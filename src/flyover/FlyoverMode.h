//
// Flyover: `--flyover <file.json>` flies a PathCamera along a route, reports on stdout and takes commands on stdin.
//
// `--wow <dir> --product <name>` (each also as `--x=value`) opens that install's product, as `<dir>/.build.info`
// spells it, instead of the first storage in cascStorages.json. One without the other is a usage error.
// A failure to open the install exits 2.
//
// The periodic `flyover pos <x> <y> <hx> <hy> <z>` line is the camera in use: its world x and y, the unit heading over the ground, and its z.
//
// `--db <file>` (also `--db=<file>`) opens that SQLite file as the client database instead of `./export.db3`.
// A file that is missing or fails to open exits 2 before any window exists.
//
// The road recorder's ghost line, on stdin: `roadlines <file.json>` replaces the baked roads drawn around the camera with the file's
// `{"lines": [[x, y, z, x, y, z, ...], ...]}`, one flat polyline each, and `roadlines none` removes them; `take <x> <y> <z>` adds a
// point to the take drawn in another colour, and `take clear` empties it. `posinterval <ms>` sets how often `pos` is sent, 500 at
// the start. See RoadLines.h.
//
// `dump <file.png>` on stdin writes the next frame drawn to that PNG and answers with one `flyover dump {json}` line,
// the viewer's side of a debug dump: see FlyoverMode.cpp. The viewer ignores its own window's mouse and keyboard; the host
// reads the input and sends `mode`, `look`, `move`, `freespeed` and `dump`.
//
// The `flyover world {json}` line, one for every frame drawn once `ready` has gone, is the state a host's script reads: see
// FlyoverMode.cpp. The viewer holds no policy; the script sets Config fields by the `config` command.
//

#ifndef AWEBWOWVIEWERCPP_FLYOVERMODE_H
#define AWEBWOWVIEWERCPP_FLYOVERMODE_H

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <mathfu/glsl_mappings.h>
#include "../../wowViewerLib/src/engine/ApiContainer.h"

struct GLFWwindow;
class FrontendUI;
class SceneWindow;
class PathCamera;
class FirstPersonCamera;
class HeightBake;
class ICamera;
struct MapRenderPlan;

struct FlyoverRoute {
    int mapId = 0;
    int wdtFileId = 0;
    std::vector<mathfu::vec3> points;
    float speed = 32.0f;
    float lookAhead = 60.0f;
    // Yards the path camera rides above the points.
    float eye = 0.0f;
    bool loop = true;
    bool freeMode = false;
    // A route of one point: free mode only, a switch to the path is refused.
    bool freeOnly = false;
    int timeOfDay = -1;
    // The free camera's starting facing, as `yawDegrees` and `pitchDegrees` in the file, which come together and are read only in free mode.
    // The convention is the debug dump's: yaw 0 faces +x and grows toward +y, pitch is positive upward. Without them the camera
    // faces along the route.
    bool hasFacing = false;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    // Config fields as name and value text, in file order.
    std::vector<std::pair<std::string, std::string>> config;
};

// The install `--wow` and `--product` name; both empty when neither was given.
struct FlyoverInstall {
    std::string wowDir;
    std::string product;
    bool given() const { return !wowDir.empty(); }
};

class FlyoverMode {
public:
    // True when the arguments hold `--flyover <file>`, with the file's path in routeFile.
    static bool findArgument(int argc, char *argv[], std::string &routeFile);
    // Reads `--wow` and `--product`. False with the usage error in error when only one is present or one lacks a value.
    static bool findInstall(int argc, char *argv[], FlyoverInstall &install, std::string &error);
    // Reads `--db`; file stays empty when it is absent. False with the usage error in error when it lacks a value.
    static bool findDatabase(int argc, char *argv[], std::string &file, std::string &error);
    // Reads `--heights` and `--bake`, each the build's folder of baked heights; each stays empty when absent. False with the usage error in
    // error when one lacks a value.
    static bool findHeights(int argc, char *argv[], std::string &heightsDir, std::string &bakeDir, std::string &error);
    // True when the file exists and opens as the client database. Never creates one.
    static bool canOpenDatabase(const std::string &file);
    // Moves the process's own logging to stderr and keeps the original stdout for the protocol. Call before any output.
    static void claimStdout();

    FlyoverMode(FrontendUI &frontendUI, GLFWwindow *window);
    ~FlyoverMode();

    // The folders of baked heights from `--heights` and `--bake`, set before start. A bake folder puts the viewer in bake mode.
    void setHeights(const std::string &heightsDir, const std::string &bakeDir) { m_heightsDir = heightsDir; m_bakeDir = bakeDir; }
    // Opens the install, or the first storage when none is named, then the route's map, and starts reading stdin.
    // False with the reason on stderr; failureExitCode says the status to exit with.
    bool start(const std::string &routeFile, const FlyoverInstall &install);
    int failureExitCode() const { return m_failureExitCode; }
    // Once per frame on the main thread: applies queued commands and writes the periodic lines.
    void update();
    // Seconds the draw of the frame just drawn took, for the next `world` line.
    void noteFrame(double seconds);
private:
    enum class CommandType { Flight, Pause, Play, Trim, ModePath, ModeFree, Look, Move, FreeSpeed, Route, Config, Ambient, Dump, RoadLines, Take, PosInterval, Bake, Marker, Popup, ZoneText, Quit };
    struct Command {
        CommandType type;
        std::string name;
        std::string text;
    };

    struct CommandQueue {
        std::mutex mutex;
        std::deque<Command> items;
    };

    static bool loadRoute(const std::string &file, FlyoverRoute &route, std::string &error);
    static void readStdin(std::shared_ptr<CommandQueue> queue);
    static void parseLine(const std::string &line, CommandQueue &queue);
    static void post(CommandQueue &queue, Command command);

    bool openInstall(const FlyoverInstall &install, std::string &error);

    void apply(const Command &command);
    void applyRoute(const FlyoverRoute &route, bool openMap);
    void refreshRoute(const FlyoverRoute &route);
    bool sameRoute(const FlyoverRoute &route) const;
    void applyConfig(const std::string &name, const std::string &value);
    void setMode(bool free, bool announce);
    void applyRoadLines(const std::string &file);
    void applyMarker(const std::string &text);
    void trimMap();
    void applyPopup(const std::string &text);
    void applyZoneText(const std::string &text);
    void refreshWeatherPicture();
    void takeDump(const std::string &pngFile);
    void answerDump(const std::string &pngFile, FrameCapture &&capture);
    void emitWorld(const MapRenderPlan *plan);
    std::shared_ptr<ICamera> activeCamera() const;

    int m_failureExitCode = 1;

    FrontendUI &m_frontendUI;
    GLFWwindow *m_window;
    HApiContainer m_api;
    std::shared_ptr<SceneWindow> m_scene;

    std::shared_ptr<PathCamera> m_path;
    std::shared_ptr<FirstPersonCamera> m_free;
    bool m_isFree = false;
    // Yards per millisecond, 30 yards a second at the start of every flight, since a new viewer starts each one
    float m_freeSpeed = 0.03f;
    bool m_paused = false;
    int m_mapId = -1;
    int m_wdtFileId = -1;
    FlyoverRoute m_route;
    // The markers placed by `marker`, by name: the id in the open map's WorldObjectManager, and what places the marker again.
    // Emptied when a route opens a map, since the new map's manager starts with none; a trim places each again.
    struct Marker {
        int id;
        int fileDataId;
        mathfu::vec3 pos;
        float scale;
    };
    std::map<std::string, Marker> m_markers;
    // The scene buffers' bytes once the open map had loaded; 0 until sampled, and again after every open.
    size_t m_trimBaseline = 0;

    std::string m_heightsDir;
    std::string m_bakeDir;
    std::unique_ptr<HeightBake> m_bake;
    // Shared with the stdin thread, which can outlive this object at exit.
    std::shared_ptr<CommandQueue> m_queue = std::make_shared<CommandQueue>();

    // The weatherTexture the picture in the config was loaded for.
    int m_weatherPictureId = 0;

    bool m_readySent = false;
    int m_framesWithPlan = 0;
    double m_nextAreaSample = 0;
    double m_nextDistanceReport = 0;
    double m_nextPosReport = 0;
    // Seconds between `pos` lines.
    double m_posInterval = 0.5;
    int m_lastAreaId = 0;
    int m_lastParentAreaId = 0;
    bool m_areaReported = false;

    // The last frame's draw seconds.
    double m_frameSeconds = 0;
};

#endif //AWEBWOWVIEWERCPP_FLYOVERMODE_H
