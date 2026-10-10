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

#ifndef AWEBWOWVIEWERCPP_FLYOVERMODE_H
#define AWEBWOWVIEWERCPP_FLYOVERMODE_H

#include <deque>
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

struct FlyoverRoute {
    int mapId = 0;
    int wdtFileId = 0;
    std::vector<mathfu::vec3> points;
    float speed = 32.0f;
    float lookAhead = 60.0f;
    bool loop = true;
    bool freeMode = false;
    // A route of one point: free mode only, a switch to the path is refused.
    bool freeOnly = false;
    int timeOfDay = -1;
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
    // True when the file exists and opens as the client database. Never creates one.
    static bool canOpenDatabase(const std::string &file);
    // Moves the process's own logging to stderr and keeps the original stdout for the protocol. Call before any output.
    static void claimStdout();

    FlyoverMode(FrontendUI &frontendUI, GLFWwindow *window);
    ~FlyoverMode();

    // Opens the install, or the first storage when none is named, then the route's map, and starts reading stdin.
    // False with the reason on stderr; failureExitCode says the status to exit with.
    bool start(const std::string &routeFile, const FlyoverInstall &install);
    int failureExitCode() const { return m_failureExitCode; }
    // Once per frame on the main thread: applies queued commands and writes the periodic lines.
    void update();
    // The F key.
    void requestToggleMode();
private:
    enum class CommandType { Speed, Pause, Play, ModePath, ModeFree, ModeToggle, Look, Move, FreeSpeed, Route, Config, ConfigDump, Quit };
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

    int m_failureExitCode = 1;

    FrontendUI &m_frontendUI;
    GLFWwindow *m_window;
    HApiContainer m_api;
    std::shared_ptr<SceneWindow> m_scene;

    std::shared_ptr<PathCamera> m_path;
    std::shared_ptr<FirstPersonCamera> m_free;
    bool m_isFree = false;
    float m_freeSpeed = 0.3f;
    bool m_paused = false;
    int m_mapId = -1;
    int m_wdtFileId = -1;
    FlyoverRoute m_route;

    // Shared with the stdin thread, which can outlive this object at exit.
    std::shared_ptr<CommandQueue> m_queue = std::make_shared<CommandQueue>();

    bool m_readySent = false;
    int m_framesWithPlan = 0;
    double m_nextAreaSample = 0;
    double m_nextDistanceReport = 0;
    int m_lastAreaId = 0;
    int m_lastParentAreaId = 0;
    bool m_areaReported = false;
};

#endif //AWEBWOWVIEWERCPP_FLYOVERMODE_H
