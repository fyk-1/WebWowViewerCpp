//
// Flyover: `--flyover <file.json>` flies a PathCamera along a route, reports on stdout and takes commands on stdin.
//

#include "FlyoverMode.h"

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
the scene and the cameras live.

Far plane: flyover sets farPlaneForCulling to 1000 at startup, before the route file's config is applied,
so fog at full strength at 1000 yards has geometry behind it. The normal UI mode keeps 400.

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
        if (route.points.size() < 2) {
            error = "points needs at least two entries";
            return false;
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

bool FlyoverMode::start(const std::string &routeFile) {
    FlyoverRoute route;
    std::string error;
    if (!loadRoute(routeFile, route, error)) {
        std::cerr << "flyover: " << error << std::endl;
        return false;
    }

    if (!m_frontendUI.openFirstStorage()) {
        std::cerr << "flyover: could not open the first storage in cascStorages.json" << std::endl;
        return false;
    }

    m_scene = m_frontendUI.getBackgroundScene();
    m_api = m_frontendUI.getApi();
    // Before the route's config, so a file value still wins.
    m_api->getConfig()->farPlaneForCulling = 1000;
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
    if (free) {
        if (!m_isFree || !m_free) {
            // The free camera starts where the path camera is, looking where it looks.
            auto matrices = m_path->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
            mathfu::vec3 position = matrices->cameraPos.xyz();
            mathfu::vec3 lookAt = matrices->lookAt.xyz();

            m_free = std::make_shared<FirstPersonCamera>();
            m_free->setCameraPos(position.x, position.y, position.z);
            m_free->setCameraLookAt(lookAt.x, lookAt.y, lookAt.z);
            // The scene's own free-camera speed.
            m_free->setMovementSpeed(0.3f);
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
    }
}
