//
// Height bake: `--bake <dir>` makes the viewer render the ground of one continent tile with its buildings from straight above, as a
// depth image, and write the heights of it to the far terrain's cache (FarHeights). The host drives it with `bake <row> <col>
// <timeoutSeconds>` on stdin and reads one `flyover bake` line back per tile:
//
//   flyover bake <row> <col> ok <waitS> <renderS> <stepN> <stepS> <stepE> <stepW>
//   flyover bake <row> <col> timeout <s> <pending>/<total> (m2 <n> wmo <n> groups <n>) settled or loading
//   flyover bake <row> <col> error <reason>
//
// The steps are the largest height difference in yards along the edge the tile shares with the neighbour north (row - 1), south
// (row + 1), east (column + 1) and west (column - 1), `-` where that neighbour has no file. The renderer is set by the Lua file
// the host's `--script` names; nothing here sets a Config field.
//

#ifndef AWEBWOWVIEWERCPP_HEIGHTBAKE_H
#define AWEBWOWVIEWERCPP_HEIGHTBAKE_H

#include <functional>
#include <memory>
#include <string>
#include "../../wowViewerLib/src/engine/ApiContainer.h"
#include "../../wowViewerLib/src/gapi/interface/IDevice.h"

class SceneWindow;
class BakeCamera;
class ICamera;
struct MapRenderPlan;
struct HeightTile;

class HeightBake {
public:
    // `buildDir` is the build's folder of the cache; a map's folder is under it. `emit` writes one protocol line.
    HeightBake(std::shared_ptr<SceneWindow> scene, HApiContainer api, std::string buildDir, std::function<void(const std::string &)> emit);
    ~HeightBake();

    // Starts one tile of the map, which answers with its line. Refused with an error line while another is in flight.
    void request(int mapId, int row, int col, double timeoutSeconds, double now);
    // Once per frame on the main thread, with the last plan.
    void update(double now, const MapRenderPlan *plan);
    // The camera in use once a tile has been asked for; null before.
    std::shared_ptr<ICamera> camera() const;

private:
    enum class Phase { Idle, Waiting, Capturing };

    void finish(const std::string &line);
    bool reduce(const DepthCapture &capture, HeightTile &tile, std::string &error);

    std::shared_ptr<SceneWindow> m_scene;
    HApiContainer m_api;
    std::string m_buildDir;
    std::function<void(const std::string &)> m_emit;
    std::shared_ptr<BakeCamera> m_camera;

    Phase m_phase = Phase::Idle;
    int m_mapId = 0, m_row = 0, m_col = 0;
    double m_timeout = 0, m_started = 0, m_armed = 0, m_quietSince = 0;
    float m_x = 0, m_y = 0, m_half = 0;
    int m_stableFrames = 0;
    // The capture, filled on the thread that draws and read by update on the same one.
    bool m_captured = false;
    DepthCapture m_capture;
    int m_pending = 0, m_total = 0;
    int m_pendingBy[3] = {0, 0, 0};
    bool m_loadSettled = false;
};

#endif //AWEBWOWVIEWERCPP_HEIGHTBAKE_H
