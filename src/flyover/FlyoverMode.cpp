//
// Flyover: `--flyover <file.json>` flies a PathCamera along a route, reports on stdout and takes commands on stdin.
//
// `--wow <dir> --product <name>` (each also as `--x=value`) opens that install's product, as `<dir>/.build.info`
// spells it, instead of the first storage in cascStorages.json. One without the other is a usage error.
// A failure to open the install exits 2.
//
// `--db <file>` (also `--db=<file>`) opens that SQLite file as the client database instead of `./export.db3`.
// A file that is missing or fails to open exits 2 before any window exists.
//

#include "FlyoverMode.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <GLFW/glfw3.h>

#include "FlyoverConfig.h"
#include "../ui/FrontendUI.h"
#include "../ui/childWindow/sceneWindow/SceneWindow.h"
#include "../database/CSqliteDB.h"
#include "../database/buildInfoParser/buildInfoParser.h"
#include "../../3rdparty/nlohmann/json.hpp"
#include "../../wowViewerLib/src/engine/camera/firstPersonCamera.h"
#include "../../wowViewerLib/src/engine/camera/pathCamera.h"

using json = nlohmann::json;

/*
robots@Impl: Protocol output and stdin.

claimStdout duplicates the process's stdout into a private stream and points fd 1 at stderr, so the
viewer's own logging, through cout or printf, lands on stderr and only protocol lines reach the host's pipe.
Every protocol line is one fputs and a flush.

Windows reads stdin with ReadFile on the console or pipe handle, not through the CRT. A CRT read holds the
stdin stream lock while it blocks, and process exit then waits on that lock when it flushes the streams.
The reader thread is detached and holds only the shared queue, never the FlyoverMode, so a line arriving
during exit touches nothing freed.

Commands are parsed to name and text on that thread and validated on the main thread, where the Config,
the scene and the cameras live. `look <dx> <dy>`, `move <w|a|s|d|q|e> <1|0>` and `freespeed <steps>` (each step 1.25x, kept across switches) steer the free camera for a
host that takes the input itself, the window behind the desktop icons getting none; look does nothing in path
mode, and a move still held at a switch goes with the camera, since every switch to free mode starts a new one.

Far plane: flyover sets farPlaneForCulling to 1000 and farPlaneForCullingAir to 1500 at startup, before the route
file's config is applied, so fog at full strength at 1000 yards has geometry behind it. The normal UI mode keeps 400.

Frame rate: flyover sets frameRateCap to 30 the same way. The swapchain presents in mailbox mode, which
never waits for vsync, so an uncapped viewer renders as fast as the GPU allows; a camera at taxi speed
reads smooth at 30. The normal UI mode stays uncapped.

Render scale: flyover sets fsrUpscaling on and renderScale to 0.59, so the scene renders at 59 % of the window's
width and height (35 % of its pixels) and FSR upscales it to the window. On the iGPU a 4K window took nearly the
whole GPU where a smaller monitor took half, so the pixel count is the cost. Where FSR is unavailable the final
pass stretches the scene instead. The normal UI mode renders at 1, without FSR.

FSR history: the camera jumping makes the upscaler drop its history. A route that opens a map or changes the
points, and every switch between the path and the free camera, set fsrResetRequested; the renderer consumes it
on its next frame.

End of stdin queues quit, so a host that dies leaves no orphaned viewer. A route command for the same map
whose point list matches the current route (count, and coordinate sum within 0.01) is a refresh: distance
and camera stay, and speed, lookAhead, loop, mode and config apply. A differing point list restarts the
distance at 0, and a different map reopens.

Ready is sent once the scene has produced a render plan and two more frames have run. The area comes from
that plan, the same values the viewer's own stats window shows, not from Map::getAdtAreaId: that function
reads the tile table the culling threads write, and a tile still loading is a null pointer in it.
*/

namespace {
    FILE *g_protocolOut = nullptr;
    std::mutex g_outputMutex;

    void emit(const std::string &line) {
        std::lock_guard<std::mutex> lock(g_outputMutex);
        FILE *out = g_protocolOut ? g_protocolOut : stdout;
        std::fputs(line.c_str(), out);
        std::fputc('\n', out);
        std::fflush(out);
    }

    void emitError(const std::string &what, const std::string &reason) {
        emit("flyover error " + what + " " + reason);
    }

    std::string trim(const std::string &s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    // A JSON config value as the text the stdin command would carry.
    bool jsonToText(const json &value, std::string &out) {
        if (value.is_boolean()) {
            out = value.get<bool>() ? "true" : "false";
        } else if (value.is_number()) {
            out = value.dump();
        } else if (value.is_string()) {
            out = value.get<std::string>();
        } else if (value.is_array()) {
            out.clear();
            for (const auto &item : value) {
                if (!item.is_number()) return false;
                if (!out.empty()) out += ",";
                out += item.dump();
            }
        } else {
            return false;
        }
        return true;
    }

    enum class Found { Absent, Value, NoValue };

    // `--name value` or `--name=value`.
    Found findValue(int argc, char *argv[], const std::string &name, std::string &value) {
        for (int i = 1; i < argc; i++) {
            std::string argument = argv[i];
            if (argument == name) {
                if (i + 1 >= argc) return Found::NoValue;
                value = argv[i + 1];
                return Found::Value;
            }
            if (argument.rfind(name + "=", 0) == 0) {
                value = argument.substr(name.size() + 1);
                return value.empty() ? Found::NoValue : Found::Value;
            }
        }
        return Found::Absent;
    }
}

bool FlyoverMode::findArgument(int argc, char *argv[], std::string &routeFile) {
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        if (argument == "--flyover" && i + 1 < argc) {
            routeFile = argv[i + 1];
            return true;
        }
        if (argument.rfind("--flyover=", 0) == 0) {
            routeFile = argument.substr(std::strlen("--flyover="));
            return !routeFile.empty();
        }
    }
    return false;
}

bool FlyoverMode::findInstall(int argc, char *argv[], FlyoverInstall &install, std::string &error) {
    const Found wow = findValue(argc, argv, "--wow", install.wowDir);
    const Found product = findValue(argc, argv, "--product", install.product);
    if (wow == Found::NoValue || product == Found::NoValue) {
        error = std::string(wow == Found::NoValue ? "--wow" : "--product") + " needs a value";
        return false;
    }
    if ((wow == Found::Value) != (product == Found::Value)) {
        error = "--wow and --product go together";
        return false;
    }
    return true;
}

bool FlyoverMode::findDatabase(int argc, char *argv[], std::string &file, std::string &error) {
    if (findValue(argc, argv, "--db", file) == Found::NoValue) {
        error = "--db needs a value";
        return false;
    }
    return true;
}

bool FlyoverMode::canOpenDatabase(const std::string &file) {
    // CSqliteDB opens with OPEN_CREATE, so a missing file is turned away first rather than made.
    if (!std::ifstream(file, std::ios::binary).is_open()) return false;
    try {
        CSqliteDB probe(file);
        return true;
    } catch (...) {
        return false;
    }
}

void FlyoverMode::claimStdout() {
    std::fflush(stdout);

#ifdef _WIN32
    int copy = _dup(_fileno(stdout));
    if (copy >= 0) g_protocolOut = _fdopen(copy, "w");
    if (_dup2(_fileno(stderr), _fileno(stdout)) != 0) {
        // No stderr to point at, so logging goes nowhere rather than into the protocol.
        std::freopen("NUL", "w", stdout);
    }
#else
    int copy = dup(fileno(stdout));
    if (copy >= 0) g_protocolOut = fdopen(copy, "w");
    if (dup2(fileno(stderr), fileno(stdout)) < 0) {
        std::freopen("/dev/null", "w", stdout);
    }
#endif

    if (!g_protocolOut) g_protocolOut = stdout;
}

FlyoverMode::FlyoverMode(FrontendUI &frontendUI, GLFWwindow *window) :
    m_frontendUI(frontendUI), m_window(window) {
}

FlyoverMode::~FlyoverMode() = default;

bool FlyoverMode::loadRoute(const std::string &file, FlyoverRoute &route, std::string &error) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        error = "cannot open " + file;
        return false;
    }

    try {
        json root = json::parse(stream);

        route.mapId = root.at("mapId").get<int>();
        route.wdtFileId = root.at("wdtFileId").get<int>();

        route.points.clear();
        // A point may carry `d` (cumulative yards) and `delay`. The path measures its own length and a
        // node's delay is ignored for now, so neither is read.
        for (const auto &point : root.at("points")) {
            route.points.emplace_back(point.at("x").get<float>(), point.at("y").get<float>(),
                                      point.at("z").get<float>());
        }
        route.speed = root.value("speed", 32.0f);
        route.lookAhead = root.value("lookAhead", 60.0f);
        route.loop = root.value("loop", true);
        route.timeOfDay = root.value("timeOfDay", -1);

        std::string mode = root.value("mode", std::string("path"));
        if (mode == "path") {
            route.freeMode = false;
        } else if (mode == "free") {
            route.freeMode = true;
        } else {
            error = "mode must be path or free";
            return false;
        }
        // One point is a free flight from there: the path camera gets a second point 100 yards north, past its look-ahead, to stand on and look
        // along, and the route stays in free mode.
        if (route.points.size() == 1 && route.freeMode) {
            route.points.push_back(route.points[0] + mathfu::vec3(100.0f, 0.0f, 0.0f));
            route.freeOnly = true;
        }
        if (route.points.size() < 2) {
            error = "points needs at least two entries, or one with mode free";
            return false;
        }
        if (route.speed < 0) {
            error = "speed must not be negative";
            return false;
        }

        route.config.clear();
        if (root.contains("config")) {
            for (const auto &item : root.at("config").items()) {
                std::string text;
                if (!jsonToText(item.value(), text)) {
                    error = "config " + item.key() + " has an unsupported value";
                    return false;
                }
                route.config.emplace_back(item.key(), text);
            }
        }
    } catch (const std::exception &exception) {
        error = exception.what();
        return false;
    }

    return true;
}

bool FlyoverMode::openInstall(const FlyoverInstall &install, std::string &error) {
    const std::string buildInfoPath = install.wowDir + "/.build.info";
    std::ifstream file(buildInfoPath, std::ios::binary);
    if (!file.is_open()) {
        error = "cannot read " + buildInfoPath;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // The parser returns an unordered set, so each row goes through it on its own to keep the file's order.
    std::vector<std::string> lines;
    std::string line;
    for (char c : content) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(line);

    BuildDefinition buildDef;
    bool found = false;
    try {
        for (size_t i = 1; i < lines.size() && !found; i++) {
            if (lines[i].empty()) continue;
            std::string row = lines[0] + "\n" + lines[i];
            for (const auto &definition : BuildInfoParser::parseFileContent(row)) {
                if (definition.productName == install.product) {
                    buildDef = definition;
                    found = true;
                }
            }
        }
    } catch (const std::exception &exception) {
        error = buildInfoPath + ": " + exception.what();
        return false;
    }
    if (!found) {
        error = buildInfoPath + " lists no product " + install.product;
        return false;
    }

    if (!m_frontendUI.openBuild(install.wowDir, buildDef)) {
        error = "could not open " + install.product + " in " + install.wowDir;
        return false;
    }
    return true;
}

bool FlyoverMode::start(const std::string &routeFile, const FlyoverInstall &install) {
    FlyoverRoute route;
    std::string error;
    if (!loadRoute(routeFile, route, error)) {
        std::cerr << "flyover: " << error << std::endl;
        return false;
    }

    if (install.given()) {
        if (!openInstall(install, error)) {
            std::cerr << "flyover: " << error << std::endl;
            m_failureExitCode = 2;
            return false;
        }
    } else if (!m_frontendUI.openFirstStorage()) {
        std::cerr << "flyover: could not open the first storage in cascStorages.json" << std::endl;
        return false;
    }

    m_scene = m_frontendUI.getBackgroundScene();
    m_api = m_frontendUI.getApi();
    // Before the route's config, so a file value still wins.
    m_api->getConfig()->farPlaneForCulling = 1000;
    m_api->getConfig()->farPlaneForCullingAir = 1500;
    m_api->getConfig()->frameRateCap = 30;
    m_api->getConfig()->fsrUpscaling = true;
    m_api->getConfig()->renderScale = 0.59f;
    applyRoute(route, true);

    std::thread(&FlyoverMode::readStdin, m_queue).detach();
    return true;
}

void FlyoverMode::requestToggleMode() {
    post(*m_queue, {CommandType::ModeToggle, "", ""});
}
void FlyoverMode::post(CommandQueue &queue, Command command) {
    std::lock_guard<std::mutex> lock(queue.mutex);
    queue.items.push_back(std::move(command));
}

void FlyoverMode::readStdin(std::shared_ptr<CommandQueue> queue) {
    std::string pending;

    auto drainLines = [&]() {
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            parseLine(pending.substr(0, newline), *queue);
            pending.erase(0, newline + 1);
        }
    };

#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) return;

    char buffer[512];
    DWORD got = 0;
    while (ReadFile(input, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        pending.append(buffer, got);
        drainLines();
    }
#else
    char buffer[512];
    ssize_t got;
    while ((got = read(0, buffer, sizeof(buffer))) > 0) {
        pending.append(buffer, got);
        drainLines();
    }
#endif
    // The host owns the process through this pipe, so end of input or a read error means the host is gone.
    post(*queue, {CommandType::Quit, "", ""});
}

void FlyoverMode::parseLine(const std::string &rawLine, CommandQueue &queue) {
    std::string line = trim(rawLine);
    if (line.empty()) return;

    size_t split = line.find_first_of(" \t");
    std::string word = line.substr(0, split);
    std::string rest = split == std::string::npos ? "" : trim(line.substr(split));

    if (word == "speed") {
        post(queue, {CommandType::Speed, "", rest});
    } else if (word == "pause") {
        post(queue, {CommandType::Pause, "", ""});
    } else if (word == "play") {
        post(queue, {CommandType::Play, "", ""});
    } else if (word == "mode") {
        if (rest == "path") {
            post(queue, {CommandType::ModePath, "", ""});
        } else if (rest == "free") {
            post(queue, {CommandType::ModeFree, "", ""});
        } else {
            emitError("mode", "expected path or free");
        }
    } else if (word == "look") {
        post(queue, {CommandType::Look, "", rest});
    } else if (word == "freespeed") {
        post(queue, {CommandType::FreeSpeed, "", rest});
    } else if (word == "move") {
        size_t keyEnd = rest.find_first_of(" \t");
        std::string key = rest.substr(0, keyEnd);
        std::string state = keyEnd == std::string::npos ? "" : trim(rest.substr(keyEnd));
        post(queue, {CommandType::Move, key, state});
    } else if (word == "route") {
        post(queue, {CommandType::Route, "", rest});
    } else if (word == "config") {
        if (rest.empty()) {
            post(queue, {CommandType::ConfigDump, "", ""});
        } else {
            size_t nameEnd = rest.find_first_of(" \t");
            std::string name = rest.substr(0, nameEnd);
            std::string value = nameEnd == std::string::npos ? "" : trim(rest.substr(nameEnd));
            post(queue, {CommandType::Config, name, value});
        }
    } else if (word == "quit") {
        post(queue, {CommandType::Quit, "", ""});
    } else {
        emitError(word, "unknown command");
    }
}

void FlyoverMode::apply(const Command &command) {
    switch (command.type) {
        case CommandType::Speed: {
            char *end = nullptr;
            double speed = std::strtod(command.text.c_str(), &end);
            if (command.text.empty() || end != command.text.c_str() + command.text.size() || !(speed >= 0)) {
                emitError("speed", "expected a number of yards per second, 0 or more");
            } else if (m_path) {
                m_path->setMovementSpeed((float) speed);
            }
            break;
        }
        case CommandType::Pause:
            m_paused = true;
            if (m_path) m_path->setPaused(true);
            break;
        case CommandType::Play:
            m_paused = false;
            if (m_path) m_path->setPaused(false);
            break;
        case CommandType::ModePath:
            setMode(false, true);
            break;
        case CommandType::ModeFree:
            setMode(true, true);
            break;
        case CommandType::ModeToggle:
            setMode(!m_isFree, true);
            break;
        case CommandType::Look: {
            float dx = 0, dy = 0;
            if (std::sscanf(command.text.c_str(), "%f %f", &dx, &dy) != 2) {
                emitError("look", "expected two numbers, pixels across and down");
            } else if (m_isFree && m_free) {
                // The same scale as a mouse drag in the viewer's own window.
                m_free->addHorizontalViewDir(dx / 4.0f);
                m_free->addVerticalViewDir(dy / 4.0f);
            }
            break;
        }
        case CommandType::FreeSpeed: {
            // Steps of a quarter faster or slower, a mouse wheel's notches.
            float steps = 0;
            if (std::sscanf(command.text.c_str(), "%f", &steps) != 1) {
                emitError("freespeed", "expected a number of steps, negative for slower");
            } else {
                m_freeSpeed = std::clamp(m_freeSpeed * std::pow(1.25f, steps), 0.02f, 20.0f);
                if (m_free) m_free->setMovementSpeed(m_freeSpeed);
            }
            break;
        }
        case CommandType::Move: {
            const std::string &key = command.name;
            bool down = command.text == "1";
            if ((command.text != "0" && command.text != "1") || key.size() != 1 || std::string("wasdqe").find(key[0]) == std::string::npos) {
                emitError("move", "expected one of w a s d q e, then 1 or 0");
            } else if (m_free) {
                switch (key[0]) {
                    case 'w': down ? m_free->startMovingForward() : m_free->stopMovingForward(); break;
                    case 's': down ? m_free->startMovingBackwards() : m_free->stopMovingBackwards(); break;
                    case 'a': down ? m_free->startStrafingLeft() : m_free->stopStrafingLeft(); break;
                    case 'd': down ? m_free->startStrafingRight() : m_free->stopStrafingRight(); break;
                    case 'q': down ? m_free->startMovingUp() : m_free->stopMovingUp(); break;
                    case 'e': down ? m_free->startMovingDown() : m_free->stopMovingDown(); break;
                }
            }
            break;
        }
        case CommandType::Route: {
            FlyoverRoute route;
            std::string error;
            if (!loadRoute(command.text, route, error)) {
                emitError("route", error);
            } else {
                bool sameMap = route.mapId == m_mapId && route.wdtFileId == m_wdtFileId;
                if (sameMap && sameRoute(route)) {
                    refreshRoute(route);
                } else {
                    applyRoute(route, !sameMap);
                }
            }
            break;
        }
        case CommandType::Config:
            if (command.text.empty()) {
                emitError("config " + command.name, "missing value");
            } else {
                applyConfig(command.name, command.text);
            }
            break;
        case CommandType::ConfigDump:
            emit("flyover config " + FlyoverConfig::dump(*m_api->getConfig()));
            break;
        case CommandType::Quit:
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            break;
    }
}

void FlyoverMode::applyConfig(const std::string &name, const std::string &value) {
    std::string reason = FlyoverConfig::set(*m_api->getConfig(), name, value);
    if (!reason.empty()) {
        emitError("config " + name, reason);
    }
}

bool FlyoverMode::sameRoute(const FlyoverRoute &route) const {
    if (!m_path || route.points.size() != m_route.points.size()) return false;

    // Count and coordinate sum, to a tolerance, so a re-sent file that round-trips its floats still matches.
    auto sum = [](const std::vector<mathfu::vec3> &points) {
        double total = 0;
        for (const auto &point : points) total += (double) point.x + point.y + point.z;
        return total;
    };
    return std::fabs(sum(route.points) - sum(m_route.points)) <= 0.01;
}

void FlyoverMode::refreshRoute(const FlyoverRoute &route) {
    // Same map and same points: the flight goes on from where it is, and only the parameters change.
    // timeOfDay is left alone, since applying it would reopen the map.
    for (const auto &entry : route.config) {
        applyConfig(entry.first, entry.second);
    }

    m_path->setMovementSpeed(route.speed);
    m_path->setLookAhead(route.lookAhead);
    m_path->setLoop(route.loop);
    m_route = route;
    setMode(route.freeMode, false);
}

void FlyoverMode::applyRoute(const FlyoverRoute &route, bool openMap) {
    m_route = route;
    m_api->getConfig()->fsrResetRequested = true;
    if (openMap) {
        const mathfu::vec3 &start = route.points.front();
        m_scene->openMapByIdAndWDTId(route.mapId, route.wdtFileId, start.x, start.y, start.z, route.timeOfDay);
        m_mapId = route.mapId;
        m_wdtFileId = route.wdtFileId;
        m_areaReported = false;
    }

    for (const auto &entry : route.config) {
        applyConfig(entry.first, entry.second);
    }

    m_path = std::make_shared<PathCamera>(route.points, route.loop, route.speed, route.lookAhead);
    m_path->setPaused(m_paused);
    m_free = nullptr;
    m_isFree = false;
    setMode(route.freeMode, false);
}

void FlyoverMode::setMode(bool free, bool announce) {
    if (!free && m_route.freeOnly) return;
    if (free) {
        if (!m_isFree || !m_free) {
            // The free camera starts where the path camera is, looking where it looks.
            auto matrices = m_path->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
            mathfu::vec3 position = matrices->cameraPos.xyz();
            mathfu::vec3 lookAt = matrices->lookAt.xyz();

            m_free = std::make_shared<FirstPersonCamera>();
            m_free->setCameraPos(position.x, position.y, position.z);
            m_free->setCameraLookAt(lookAt.x, lookAt.y, lookAt.z);
            // The scene's own free-camera speed at first, then whatever `freespeed` made it.
            m_free->setMovementSpeed(m_freeSpeed);
        }
        m_scene->setCamera(m_free);
    } else {
        if (m_isFree && m_free) {
            // The path resumes at the distance it last had, its view eased from where the free camera looks.
            auto matrices = m_free->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
            m_path->seedViewDirection(matrices->lookAt.xyz() - matrices->cameraPos.xyz());
        }
        m_scene->setCamera(m_path);
    }

    bool changed = free != m_isFree;
    if (changed) m_api->getConfig()->fsrResetRequested = true;
    m_isFree = free;
    if (announce && changed) {
        emit(std::string("flyover mode ") + (free ? "free" : "path"));
    }
}

void FlyoverMode::update() {
    std::deque<Command> commands;
    {
        std::lock_guard<std::mutex> lock(m_queue->mutex);
        commands.swap(m_queue->items);
    }
    for (const auto &command : commands) {
        apply(command);
    }

    if (!m_scene || !m_scene->hasRenderer()) return;

    double now = glfwGetTime();
    auto plan = m_scene->getLastPlan();

    if (!m_readySent) {
        if (plan && ++m_framesWithPlan >= 2) {
            emit("flyover ready");
            emit("flyover config " + FlyoverConfig::dump(*m_api->getConfig()));
            m_readySent = true;
            m_nextAreaSample = now;
            m_nextDistanceReport = now + 0.5;
        }
        return;
    }

    if (plan && now >= m_nextAreaSample) {
        m_nextAreaSample = now + 0.25;
        if (!m_areaReported || plan->areaId != m_lastAreaId || plan->parentAreaId != m_lastParentAreaId) {
            m_areaReported = true;
            m_lastAreaId = plan->areaId;
            m_lastParentAreaId = plan->parentAreaId;
            emit("flyover area " + std::to_string(m_lastAreaId) + " " + std::to_string(m_lastParentAreaId));
        }
    }

    if (now >= m_nextDistanceReport) {
        m_nextDistanceReport = now + 0.5;
        char text[64];
        std::snprintf(text, sizeof(text), "flyover d %.1f", m_path ? m_path->getDistance() : 0.0f);
        emit(text);
        // The camera in use, path or free, as world x and y and its heading over the ground.
        std::shared_ptr<ICamera> camera = m_isFree && m_free ? std::static_pointer_cast<ICamera>(m_free) : std::static_pointer_cast<ICamera>(m_path);
        if (camera) {
            auto matrices = camera->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
            mathfu::vec3 position = matrices->cameraPos.xyz();
            mathfu::vec3 ahead = matrices->lookAt.xyz() - position;
            float across = std::sqrt(ahead.x * ahead.x + ahead.y * ahead.y);
            if (across < 1e-4f) across = 1.0f;
            std::snprintf(text, sizeof(text), "flyover pos %.1f %.1f %.3f %.3f", position.x, position.y, ahead.x / across, ahead.y / across);
            emit(text);
        }
    }
}
