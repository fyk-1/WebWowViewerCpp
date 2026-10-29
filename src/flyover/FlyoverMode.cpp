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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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
#include "HeightBake.h"
#include "../ui/FrontendUI.h"
#include "../ui/childWindow/sceneWindow/SceneWindow.h"
#include "../database/CSqliteDB.h"
#include "../database/buildInfoParser/buildInfoParser.h"
#include "../../3rdparty/nlohmann/json.hpp"
#include "../../wowViewerLib/src/engine/algorithms/mathHelper.h"
#include "../../wowViewerLib/src/engine/camera/firstPersonCamera.h"
#include "../../wowViewerLib/src/engine/camera/pathCamera.h"
#include "../../wowViewerLib/src/engine/objects/worldObject/WorldObjectManager.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../3rdparty/glfw/deps/stb_image_write.h"

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
`flight <speed> <eye> <lookAhead>` sets the path camera's yards a second, the yards it rides above the route's points and
its look-ahead, mid-flight, the same three a route file carries.

Config: the viewer starts from the library's Config and holds no policy. The route file's config carries every value the host
wants at the start, and the host's script sets the rest as the flight goes, from the `world` line (Modules/Flyover/scripts/README.md).
The pace is the main loop's (main.cpp): vsyncDivisor 1 to 4 starts a frame on every Nth vblank of the window's
monitor, and 0 leaves it to frameRateCap, at most that many frames a second, or uncapped at 0, as in the normal UI mode.

FSR history: the camera jumping makes the upscaler drop its history. A route that opens a map or changes the
points, and every switch between the path and the free camera, set fsrResetRequested; the renderer consumes it
on its next frame.

End of stdin queues quit, so a host that dies leaves no orphaned viewer. A route command for the same map
whose point list matches the current route (count, and coordinate sum within 0.01) is a refresh: distance
and camera stay, and speed, lookAhead, eye, loop, mode and config apply. A differing point list restarts the
distance at 0, and a different map reopens.

Ready is sent once the scene has produced a render plan and two more frames have run. The area comes from
that plan, the same values the viewer's own stats window shows, not from Map::getAdtAreaId: that function
reads the tile table the culling threads write, and a tile still loading is a null pointer in it.

Debug dump: `dump <file.png>` asks the device for the next frame drawn (IDevice::captureNextFrame), and when that frame
has finished writes it as a PNG and prints `flyover dump {json}`. The json holds the viewer's side of the dump: `png`
(the path as given), `image` {width, height} or else `error`, `mapId`, `camera` {x, y, z, yawDegrees, pitchDegrees},
`tile` {col, row} as the viewer indexes tiles, `terrainHeight` (null where no loaded tile is under the camera),
`areaId`, `parentAreaId`, `timeOfDay` in half-minutes from midnight, `mode`, `paused`, `distance` and `display`. The
terrain height is the plan's, taken by the culling thread, since the tile table is not safe to read from here. A dump
with no pixels still answers, with its error, so a waiting host never hangs. The PNG is written with stb_image_write,
and the json is read on the main thread when the frame finishes, so its camera is within a frame of the picture's.
A second `dump` while one waits for its frame is answered at once with its error.

World line: update() sends `flyover world {json}` once for every frame drawn, once `ready` has gone, so a script on the host runs at
the frame rate. The object holds `map`, `area` and `parentArea` (the plan's ids, 0 outside any area), `indoors` (the camera is in
an interior WMO group, the flag the weather reads), `mode` ("free", "paused" or "route"), `camera` {x, y, z, yaw, pitch} as the
dump's camera is, `terrainHeight` (the plan's ground height, null with no terrain under the camera), `altitude` (the camera's z
minus it, unsmoothed, null with no terrain), `wdlAltitude` (the camera's z minus the map's WDL ground under it, null where the far
terrain is off or the WDL has no tile there), `settled` (the plan's loadSettled), `tilesPending` (a tile the plan asked for has not
loaded), `frameTime` (seconds the draw of the last frame took, the cap's sleep excluded), `dayHalfMinutes` (currentTime) and
`weather` {type, intensity}.

Markers: `marker <name> <fileDataId> <x> <y> <z> [scale]` places the M2 of that FileDataID at the world point, upright and at the
scale (1 when absent), playing its own animation, and replaces a marker of the same name; `marker <name> none` removes one and
`marker clear` removes all. A marker is a custom M2 of the map's WorldObjectManager, so it is an exterior candidate of every frame
culled as any doodad, and is not homed to a WMO interior group: inside a building it shows only from where the outside does. A route
that opens another map drops every marker. A malformed line, or one sent before a map is open, answers `flyover error marker <reason>`.

Trim: `trim` opens the open map again in place, with a new renderer, Map and cache storage, which frees what the scene buffers grew to
during a flight. It does so only when the scene buffers hold at least twice what they held once the map had loaded, and answers
`flyover trim reloaded`; otherwise it answers `flyover trim kept`. The camera, its position and look, the pause, the markers, the route,
the config, the popup, the zone text, the road lines and the take points carry over. With no map open it answers `flyover error trim no map is open`.

Popup: `popup {"text": "...", "set": "...", "x": ..., "y": ..., "z": ...}` shows the text as the game's tooltip beside that world point
(FrontendUI::setFlyoverPopup), with the optional `set` line under it, replacing the one shown, and `popup none` hides it. A line that is neither answers `flyover error popup <reason>`.

On-screen text: `osd_text {"text": "...", "subtext": "...", "hold": true}` shows each line it names as the game's zone banner
(FrontendUI::setFlyoverOsdText), `text` the big line and `subtext` the one under it. A line fades in, holds and fades out from then, or
with `hold` stays at full opacity until it is replaced; a line it leaves out is left as it is, and an empty one is hidden. `osd_text none`
hides both. What shows when is the host's: the zone names, a transport's countdown. A line that is neither answers
`flyover error osd_text <reason>`.

Start facing: a route in free mode may carry `yawDegrees` and `pitchDegrees`, and the free camera then starts facing that
way instead of along the route. They use the dump's convention, so a dump's camera replays as a route. The free camera
takes the angles directly (FirstPersonCamera::setViewAngles) rather than a look-at point, since a point one yard away
at a coordinate of several thousand loses a tenth of a degree to float precision. Only a route that starts the camera
reads them: a refresh of the same points, a switch of mode and a `look` leave the camera as it is.
*/

namespace {
    // 🤖 A trim reloads the map when the scene buffers hold at least this many times what they held once the map had loaded.
    constexpr size_t TRIM_GROWTH = 2;

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
                if (!item.is_number() && !item.is_string()) return false;
                if (!out.empty()) out += ",";
                out += item.is_string() ? item.get<std::string>() : item.dump();
            }
        } else {
            return false;
        }
        return true;
    }

    // A camera's position and facing in the dump's convention: yaw 0 faces +x and grows toward +y, pitch is positive upward.
    struct Pose {
        mathfu::vec3 position;
        float yawDegrees;
        float pitchDegrees;
    };

    bool poseOf(const std::shared_ptr<ICamera> &camera, Pose &pose) {
        if (!camera) return false;
        auto matrices = camera->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
        pose.position = matrices->cameraPos.xyz();
        mathfu::vec3 ahead = matrices->lookAt.xyz() - pose.position;
        const float radiansToDegrees = 180.0f / (float) M_PI;
        pose.yawDegrees = std::atan2(ahead.y, ahead.x) * radiansToDegrees;
        pose.pitchDegrees = std::atan2(ahead.z, std::sqrt(ahead.x * ahead.x + ahead.y * ahead.y)) * radiansToDegrees;
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


bool FlyoverMode::findHeights(int argc, char *argv[], std::string &heightsDir, std::string &bakeDir, std::string &error) {
    if (findValue(argc, argv, "--heights", heightsDir) == Found::NoValue) {
        error = "--heights needs a value";
        return false;
    }
    if (findValue(argc, argv, "--bake", bakeDir) == Found::NoValue) {
        error = "--bake needs a value";
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
        route.eye = root.value("eye", 0.0f);
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
        route.hasFacing = root.contains("yawDegrees") || root.contains("pitchDegrees");
        if (route.hasFacing) {
            if (!root.contains("yawDegrees") || !root.contains("pitchDegrees")) {
                error = "yawDegrees and pitchDegrees go together";
                return false;
            }
            route.yawDegrees = root.at("yawDegrees").get<float>();
            route.pitchDegrees = root.at("pitchDegrees").get<float>();
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

        route.landmarks.clear();
        if (root.contains("landmarks")) {
            for (const auto &item : root.at("landmarks")) {
                IdEntry entry;
                const std::string text = item.is_string() ? item.get<std::string>() : item.is_number_integer() ? item.dump() : "";
                if (!IdEntry::parse(text, entry)) {
                    error = "landmarks entry " + item.dump() + " is neither a FileDataID nor \"<id>:<group>\"";
                    return false;
                }
                route.landmarks.push_back(entry);
            }
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
    m_api->getConfig()->farHeightsDir = m_heightsDir.empty() ? m_bakeDir : m_heightsDir;
    if (!m_bakeDir.empty()) {
        m_bake = std::make_unique<HeightBake>(m_scene, m_api, m_bakeDir, [](const std::string &line) { emit(line); });
    }
    applyRoute(route, true);

    std::thread(&FlyoverMode::readStdin, m_queue).detach();
    return true;
}

void FlyoverMode::post(CommandQueue &queue, Command command) {
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.items.push_back(std::move(command));
    }
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

    if (word == "flight") {
        post(queue, {CommandType::Flight, "", rest});
    } else if (word == "pause") {
        post(queue, {CommandType::Pause, "", ""});
    } else if (word == "play") {
        post(queue, {CommandType::Play, "", ""});
    } else if (word == "trim") {
        post(queue, {CommandType::Trim, "", ""});
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
        size_t nameEnd = rest.find_first_of(" \t");
        std::string name = rest.substr(0, nameEnd);
        std::string value = nameEnd == std::string::npos ? "" : trim(rest.substr(nameEnd));
        post(queue, {CommandType::Config, name, value});
    } else if (word == "ambient") {
        size_t nameEnd = rest.find_first_of(" \t");
        std::string name = rest.substr(0, nameEnd);
        std::string value = nameEnd == std::string::npos ? "" : trim(rest.substr(nameEnd));
        post(queue, {CommandType::Ambient, name, value});
    } else if (word == "dump") {
        post(queue, {CommandType::Dump, "", rest});
    } else if (word == "roadlines") {
        post(queue, {CommandType::RoadLines, "", rest});
    } else if (word == "take") {
        post(queue, {CommandType::Take, "", rest});
    } else if (word == "popup") {
        post(queue, {CommandType::Popup, "", rest});
    } else if (word == "osd_text") {
        post(queue, {CommandType::OsdText, "", rest});
    } else if (word == "marker") {
        post(queue, {CommandType::Marker, "", rest});
    } else if (word == "posinterval") {
        post(queue, {CommandType::PosInterval, "", rest});
    } else if (word == "bake") {
        post(queue, {CommandType::Bake, "", rest});
    } else if (word == "quit") {
        post(queue, {CommandType::Quit, "", ""});
    } else {
        emitError(word, "unknown command");
    }
}

void FlyoverMode::apply(const Command &command) {
    switch (command.type) {
        case CommandType::Flight: {
            float speed = 0, eye = 0, lookAhead = 0;
            int used = 0;
            if (std::sscanf(command.text.c_str(), "%f %f %f%n", &speed, &eye, &lookAhead, &used) != 3
                || used != (int) command.text.size() || !(speed >= 0) || !std::isfinite(speed) || !std::isfinite(eye)
                || !std::isfinite(lookAhead)) {
                emitError("flight", "expected yards per second (0 or more), the eye height and the look-ahead in yards");
                break;
            }
            m_route.speed = speed;
            m_route.eye = eye;
            m_route.lookAhead = lookAhead;
            if (m_path) {
                m_path->setMovementSpeed(speed);
                m_path->setEyeHeight(eye);
                m_path->setLookAhead(lookAhead);
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
        case CommandType::Trim:
            trimMap();
            break;
        case CommandType::ModePath:
            setMode(false, true);
            break;
        case CommandType::ModeFree:
            setMode(true, true);
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
                m_freeSpeed = std::clamp(m_freeSpeed * std::pow(1.25f, steps), 0.002f, 20.0f);
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
        case CommandType::Ambient:
            if (command.text.empty()) {
                emitError((command.type == CommandType::Ambient ? "ambient " : "config ") + command.name, "missing value");
            } else {
                applyConfig(command.name, command.text);
            }
            break;
        case CommandType::Dump:
            if (command.text.empty()) {
                emitError("dump", "expected a png path");
            } else {
                takeDump(command.text);
            }
            break;
        case CommandType::Popup:
            applyPopup(command.text);
            break;
        case CommandType::OsdText:
            applyOsdText(command.text);
            break;
        case CommandType::Marker:
            applyMarker(command.text);
            break;
        case CommandType::RoadLines:
            applyRoadLines(command.text);
            break;
        case CommandType::Take: {
            auto &lines = *m_api->getConfig()->roadLines;
            float x = 0, y = 0, z = 0;
            if (command.text == "clear") {
                lines.clearTake();
            } else if (std::sscanf(command.text.c_str(), "%f %f %f", &x, &y, &z) != 3) {
                emitError("take", "expected clear, or three numbers, a world x, y and z");
            } else {
                lines.addTakePoint(mathfu::vec3(x, y, z));
            }
            break;
        }
        case CommandType::PosInterval: {
            char *end = nullptr;
            double ms = std::strtod(command.text.c_str(), &end);
            if (command.text.empty() || end != command.text.c_str() + command.text.size() || !(ms >= 20 && ms <= 5000)) {
                emitError("posinterval", "expected milliseconds, 20 to 5000");
            } else {
                m_posInterval = ms / 1000.0;
                m_nextPosReport = 0;
            }
            break;
        }
        case CommandType::Bake: {
            int row = 0, col = 0;
            double timeout = 0;
            if (std::sscanf(command.text.c_str(), "%d %d %lf", &row, &col, &timeout) != 3 || !(timeout > 0)) {
                emitError("bake", "expected a tile row, a column and a timeout in seconds");
            } else if (!m_bake || !m_readySent) {
                emit("flyover bake " + std::to_string(row) + " " + std::to_string(col) + " error " + (m_bake ? "the viewer is not ready" : "the viewer was not started with --bake"));
            } else {
                m_bake->request(m_mapId, row, col, timeout, glfwGetTime());
            }
            break;
        }
        case CommandType::Quit:
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            break;
    }
}

void FlyoverMode::applyRoadLines(const std::string &file) {
    auto &lines = *m_api->getConfig()->roadLines;
    if (file == "none") {
        lines.clearRoads();
        return;
    }
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            emitError("roadlines", "cannot open " + file);
            return;
        }
        json document = json::parse(in);
        std::vector<std::vector<mathfu::vec3>> roads;
        for (const auto &line : document.at("lines")) {
            std::vector<mathfu::vec3> points;
            for (size_t i = 0; i + 2 < line.size(); i += 3) {
                points.emplace_back(line[i].get<float>(), line[i + 1].get<float>(), line[i + 2].get<float>());
            }
            roads.push_back(std::move(points));
        }
        lines.setRoads(roads);
    } catch (const std::exception &e) {
        emitError("roadlines", e.what());
    }
}

void FlyoverMode::applyMarker(const std::string &text) {
    std::istringstream stream(text);
    std::vector<std::string> words;
    for (std::string word; stream >> word;) words.push_back(word);

    auto manager = m_scene ? m_scene->getWorldObjectManager() : nullptr;
    if (manager == nullptr) {
        emitError("marker", "no map is open");
        return;
    }

    if (words.size() == 1 && words[0] == "clear") {
        for (const auto &[name, marker] : m_markers) manager->removeCustomM2(marker.id);
        m_markers.clear();
        return;
    }
    if (words.size() == 2 && words[1] == "none") {
        auto it = m_markers.find(words[0]);
        if (it == m_markers.end()) {
            emitError("marker", "no marker named " + words[0]);
            return;
        }
        manager->removeCustomM2(it->second.id);
        m_markers.erase(it);
        return;
    }
    if (words.size() != 5 && words.size() != 6) {
        emitError("marker", "expected clear, <name> none, or <name> <fileDataId> <x> <y> <z> [scale]");
        return;
    }

    char *end = nullptr;
    long fileDataId = std::strtol(words[1].c_str(), &end, 10);
    if (*end != '\0' || fileDataId <= 0 || fileDataId > INT_MAX) {
        emitError("marker", "expected a FileDataID above 0, got " + words[1]);
        return;
    }
    float values[4] = {0, 0, 0, 1};
    for (size_t i = 2; i < words.size(); i++) {
        float value = std::strtof(words[i].c_str(), &end);
        if (*end != '\0' || !std::isfinite(value)) {
            emitError("marker", "expected a number, got " + words[i]);
            return;
        }
        values[i - 2] = value;
    }
    if (values[3] <= 0) {
        emitError("marker", "expected a scale above 0");
        return;
    }

    auto it = m_markers.find(words[0]);
    if (it != m_markers.end()) {
        manager->removeCustomM2(it->second.id);
        m_markers.erase(it);
    }
    const mathfu::vec3 position(values[0], values[1], values[2]);
    int id = manager->addCustomM2(words[0], "", (int) fileDataId, position,
                                  mathfu::vec3(0, 0, 0), mathfu::vec3(values[3], values[3], values[3]));
    if (id < 0) {
        emitError("marker", "the map refused the model");
        return;
    }
    m_markers[words[0]] = {id, (int) fileDataId, position, values[3]};
}

void FlyoverMode::trimMap() {
    // An open whose renderer is still pending has a map; its buffers count 0, so the trim is kept.
    if (m_mapId < 0 || !m_scene) {
        emitError("trim", "no map is open");
        return;
    }
    if (m_trimBaseline == 0 || m_scene->sceneBufferBytes() < TRIM_GROWTH * m_trimBaseline) {
        emit("flyover trim kept");
        return;
    }

    std::shared_ptr<ICamera> camera = m_isFree ? std::static_pointer_cast<ICamera>(m_free) : std::static_pointer_cast<ICamera>(m_path);
    float position[3] = {0, 0, 0};
    camera->getCameraPosition(position);

    m_api->getConfig()->fsrResetRequested = true;
    m_scene->openMapByIdAndWDTId(m_mapId, m_wdtFileId, position[0], position[1], position[2], -1);
    m_scene->setCamera(camera);
    m_trimBaseline = 0;

    if (auto manager = m_scene->getWorldObjectManager()) {
        for (auto it = m_markers.begin(); it != m_markers.end();) {
            Marker &marker = it->second;
            marker.id = manager->addCustomM2(it->first, "", marker.fileDataId, marker.pos,
                                             mathfu::vec3(0, 0, 0), mathfu::vec3(marker.scale, marker.scale, marker.scale));
            if (marker.id < 0) {
                emitError("marker", "the map refused the model of " + it->first);
                it = m_markers.erase(it);
            } else {
                ++it;
            }
        }
    }
    emit("flyover trim reloaded");
}

void FlyoverMode::applyPopup(const std::string &text) {
    if (text == "none") {
        m_frontendUI.setFlyoverPopup("", "", mathfu::vec3(0, 0, 0));
        return;
    }
    try {
        json document = json::parse(text);
        std::string shown = document.at("text").get<std::string>();
        if (shown.empty()) {
            emitError("popup", "expected a text that is not empty");
            return;
        }
        std::string set = document.contains("set") ? document.at("set").get<std::string>() : "";
        m_frontendUI.setFlyoverPopup(shown, set, mathfu::vec3(document.at("x").get<float>(), document.at("y").get<float>(), document.at("z").get<float>()));
    } catch (const std::exception &e) {
        emitError("popup", std::string("expected none or {\"text\", \"set\", \"x\", \"y\", \"z\"}: ") + e.what());
    }
}

void FlyoverMode::applyOsdText(const std::string &text) {
    if (text == "none") {
        const std::string hidden;
        m_frontendUI.setFlyoverOsdText(&hidden, &hidden, false);
        return;
    }
    try {
        json document = json::parse(text);
        if (!document.is_object() || (!document.contains("text") && !document.contains("subtext"))) {
            emitError("osd_text", "expected none or {\"text\", \"subtext\", \"hold\"} with text or subtext");
            return;
        }
        std::string line, subline;
        if (document.contains("text")) line = document.at("text").get<std::string>();
        if (document.contains("subtext")) subline = document.at("subtext").get<std::string>();
        const bool hold = document.contains("hold") && document.at("hold").get<bool>();
        m_frontendUI.setFlyoverOsdText(document.contains("text") ? &line : nullptr, document.contains("subtext") ? &subline : nullptr, hold);
    } catch (const std::exception &e) {
        emitError("osd_text", std::string("expected none or {\"text\", \"subtext\", \"hold\"}: ") + e.what());
    }
}

void FlyoverMode::takeDump(const std::string &pngFile) {
    FrameCapture refused;
    if (!m_readySent) {
        refused.error = "the viewer is not ready";
    } else if (!m_api->hDevice->captureNextFrame([this, pngFile](FrameCapture &&capture) { answerDump(pngFile, std::move(capture)); })) {
        refused.error = "another dump is waiting for its frame";
    } else {
        return;
    }
    answerDump(pngFile, std::move(refused));
}

void FlyoverMode::answerDump(const std::string &pngFile, FrameCapture &&capture) {
    json dump;
    dump["png"] = pngFile;

    std::string error = capture.error;
    if (error.empty()) {
        std::string bytes;
        stbi_write_png_to_func([](void *context, void *data, int size) {
            static_cast<std::string *>(context)->append(static_cast<const char *>(data), size);
        }, &bytes, (int) capture.width, (int) capture.height, 3, capture.rgb.data(), (int) capture.width * 3);

        std::ofstream file(std::filesystem::u8path(pngFile), std::ios::binary);
        if (bytes.empty() || !file.write(bytes.data(), (std::streamsize) bytes.size())) {
            error = "cannot write " + pngFile;
        } else {
            dump["image"] = {{"width", capture.width}, {"height", capture.height}};
        }
    }
    if (!error.empty()) dump["error"] = error;

    dump["mapId"] = m_mapId;
    dump["mode"] = m_isFree ? "free" : "path";
    dump["paused"] = m_paused;
    dump["distance"] = m_path ? m_path->getDistance() : 0.0f;

    Pose pose;
    if (poseOf(activeCamera(), pose)) {
        dump["camera"] = {
            {"x", pose.position.x}, {"y", pose.position.y}, {"z", pose.position.z},
            {"yawDegrees", pose.yawDegrees}, {"pitchDegrees", pose.pitchDegrees},
        };
        dump["tile"] = {{"col", worldCoordinateToAdtIndex(pose.position.y)}, {"row", worldCoordinateToAdtIndex(pose.position.x)}};
    }

    auto plan = m_scene->getLastPlan();
    dump["areaId"] = plan ? json(plan->areaId) : json(nullptr);
    dump["parentAreaId"] = plan ? json(plan->parentAreaId) : json(nullptr);
    dump["terrainHeight"] = plan && plan->hasGroundHeight ? json(plan->groundHeight) : json(nullptr);

    const Config &config = *m_api->getConfig();
    std::vector<int> spawnEvents;
    {
        std::shared_lock lock(config.activeSpawnEvents.lock);
        spawnEvents.assign(config.activeSpawnEvents.ids.begin(), config.activeSpawnEvents.ids.end());
    }
    std::sort(spawnEvents.begin(), spawnEvents.end());
    dump["timeOfDay"] = config.currentTime;
    dump["display"] = {
        {"drawDistance", config.farPlaneForCulling},
        {"modelCullPercent", config.minM2ScreenPercent},
        {"creatures", config.renderCreatures},
        {"humanoids", config.renderHumanoids},
        {"gameObjectSpawns", config.renderGameObjectSpawns},
        {"spawnEvents", spawnEvents},
        {"liquids", config.renderLiquid},
        {"renderScale", config.renderScale},
        {"fsr", config.fsrUpscaling},
        {"fov", config.fov},
        {"farTerrain", {
            {"on", config.farTerrain},
            {"share", plan ? plan->farTerrain.share : 1.0f},
            {"only", plan && plan->farTerrain.only},
            {"extent", plan ? plan->farTerrain.extent : 0.0f},
            {"tiles", plan ? plan->farTerrain.tiles : 0},
            {"drawn", plan ? plan->farTerrain.drawn.size() : 0},
            {"textures", plan ? plan->farTerrain.textures : 0},
        }},
    };

    emit("flyover dump " + dump.dump(-1, ' ', false, json::error_handler_t::replace));
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
    m_path->setEyeHeight(route.eye);
    m_path->setLoop(route.loop);
    m_route = route;
    setMode(route.freeMode, false);
}

void FlyoverMode::applyRoute(const FlyoverRoute &route, bool openMap) {
    m_route = route;
    m_api->getConfig()->fsrResetRequested = true;
    // Before the map opens: its far terrain reads them once, when it starts.
    {
        IdSet &landmarks = m_api->getConfig()->landmarks;
        std::unique_lock lock(landmarks.lock);
        landmarks.ids.clear();
        landmarks.groups.clear();
        for (const IdEntry &entry : route.landmarks) {
            if (entry.group < 0) landmarks.ids.insert(entry.fileId);
            else landmarks.groups[entry.fileId].insert(entry.group);
        }
    }
    if (openMap) {
        const mathfu::vec3 &start = route.points.front();
        m_scene->openMapByIdAndWDTId(route.mapId, route.wdtFileId, start.x, start.y, start.z, route.timeOfDay);
        m_mapId = route.mapId;
        m_wdtFileId = route.wdtFileId;
        m_areaReported = false;
        m_markers.clear();
        m_trimBaseline = 0;
    }

    for (const auto &entry : route.config) {
        applyConfig(entry.first, entry.second);
    }

    m_path = std::make_shared<PathCamera>(route.points, route.loop, route.speed, route.lookAhead);
    m_path->setEyeHeight(route.eye);
    m_path->setPaused(m_paused);
    m_free = nullptr;
    m_isFree = false;
    setMode(route.freeMode, false);
    if (route.freeMode && route.hasFacing) m_free->setViewAngles(route.yawDegrees, route.pitchDegrees);
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
            // 30 yards a second at first, then whatever `freespeed` made it.
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
    refreshWeatherPicture();

    if (!m_scene || !m_scene->hasRenderer()) return;

    double now = glfwGetTime();
    auto plan = m_scene->getLastPlan();

    if (!m_readySent) {
        if (plan && ++m_framesWithPlan >= 2) {
            emit("flyover ready");
            m_readySent = true;
            m_nextAreaSample = now;
            m_nextDistanceReport = now + 0.5;
            m_nextPosReport = now + 0.5;
        }
        return;
    }


    if (m_bake) m_bake->update(now, plan.get());
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

    if (now >= m_nextPosReport) {
        m_nextPosReport = now + m_posInterval;
        char text[64];
        // The camera in use, path or free, as world x and y, its heading over the ground, and its z.
        if (auto camera = activeCamera()) {
            auto matrices = camera->getCameraMatrices(1.0f, 1.0f, 1.0f, 100.0f);
            mathfu::vec3 position = matrices->cameraPos.xyz();
            mathfu::vec3 ahead = matrices->lookAt.xyz() - position;
            float across = std::sqrt(ahead.x * ahead.x + ahead.y * ahead.y);
            if (across < 1e-4f) across = 1.0f;
            std::snprintf(text, sizeof(text), "flyover pos %.1f %.1f %.3f %.3f %.1f", position.x, position.y, ahead.x / across, ahead.y / across, position.z);
            emit(text);
        }
    }

    if (m_trimBaseline == 0 && m_mapId >= 0 && !m_scene->mapLoading()) m_trimBaseline = m_scene->sceneBufferBytes();

    emitWorld(plan.get());
}

std::shared_ptr<ICamera> FlyoverMode::activeCamera() const {
    if (m_bake && m_bake->camera()) return m_bake->camera();
    return m_isFree && m_free ? std::static_pointer_cast<ICamera>(m_free) : std::static_pointer_cast<ICamera>(m_path);
}

// The picture of the weather's particles follows the weatherTexture field: the texture cache loads it by FileDataID and the
// config holds it for the renderer, which draws once it has loaded.
void FlyoverMode::refreshWeatherPicture() {
    Config &config = *m_api->getConfig();
    if (config.weatherTexture == m_weatherPictureId) return;
    m_weatherPictureId = config.weatherTexture;
    config.weatherPicture->set(m_weatherPictureId > 0 ? m_api->cacheStorage->getTextureCache()->getFileId(m_weatherPictureId) : nullptr);
}

void FlyoverMode::noteFrame(double seconds) {
    m_frameSeconds = seconds;
}

void FlyoverMode::emitWorld(const MapRenderPlan *plan) {
    json world;
    world["map"] = m_mapId;
    world["area"] = plan ? plan->areaId : 0;
    world["parentArea"] = plan ? plan->parentAreaId : 0;
    world["indoors"] = plan && !plan->m_currentInteriorGroups.empty();
    world["mode"] = m_isFree ? "free" : (m_paused ? "paused" : "route");

    Pose pose;
    const bool posed = poseOf(activeCamera(), pose);
    if (posed) {
        world["camera"] = {
            {"x", pose.position.x}, {"y", pose.position.y}, {"z", pose.position.z},
            {"yaw", pose.yawDegrees}, {"pitch", pose.pitchDegrees},
        };
    } else {
        world["camera"] = nullptr;
    }
    world["terrainHeight"] = plan && plan->hasGroundHeight ? json(plan->groundHeight) : json(nullptr);
    world["altitude"] = posed && plan && plan->hasGroundHeight ? json(pose.position.z - plan->groundHeight) : json(nullptr);
    world["wdlAltitude"] = posed && plan && plan->hasWdlGround ? json(pose.position.z - plan->wdlGround) : json(nullptr);
    world["settled"] = plan && plan->loadSettled;
    world["tilesPending"] = plan && plan->tilesPending;
    world["frameTime"] = m_frameSeconds;

    const Config &config = *m_api->getConfig();
    world["dayHalfMinutes"] = config.currentTime;
    world["weather"] = {{"type", config.weatherType}, {"intensity", config.weatherIntensity}};

    emit("flyover world " + world.dump(-1, ' ', false, json::error_handler_t::replace));
}
