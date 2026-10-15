#include "HeightBake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "../ui/childWindow/sceneWindow/SceneWindow.h"
#include "../../wowViewerLib/src/engine/algorithms/mathHelper.h"
#include "../../wowViewerLib/src/engine/camera/bakeCamera.h"
#include "../../wowViewerLib/src/engine/objects/farTerrain/farHeights.h"
#include "../../wowViewerLib/src/renderer/mapScene/MapScenePlan.h"

/*
robots@Impl: One tile of the bake.

The camera is orthographic, straight down, over S * 128 / 127 yards a side (S the tile size): the tile and one 256-image pixel (S / 254)
outside it on every side. The image is 256 x 256, and the height tile 128 x 128 holds the nearest (highest) of each 2 x 2 block. The
block's centre then lies at x0 - r * S / 127, y0 - c * S / 127, the lattice FarHeights names, and the edge samples of two neighbours lie
at the same world positions.

The volume runs from z 3072 down to -1024, the range FarHeights holds, so no depth is tuned per tile.

A tile is rendered once the plan for the camera in use (its `bake` x and y) counts nothing pending in the square and the plan is
loadSettled, for three frames in a row. loadSettled stands for the geometry uploads, which the count does not see. It also covers
loading anywhere in the view, which can keep it false for good, so after kSettleGrace seconds with nothing pending the count alone
is enough. Then the next frame
drawn is read back (IDevice::captureNextDepth), and the camera is checked against the target again when the capture arrives.

A depth of 1 is the clear value and means nothing was drawn there; a texel whose 4 pixels are all clear takes the WDL's ground, and
the tile is not written when the WDL has none either.
*/
namespace {
    constexpr float kTop = 3072.0f;
    constexpr float kBottom = -1024.0f;
    constexpr int kImage = 256;
    constexpr float kClear = 0.9999f;
    constexpr int kStableFrames = 3;
    // Seconds a tile with nothing pending waits for loadSettled before it is rendered anyway: loading elsewhere in the view can
    // keep the plan from settling for good.
    constexpr double kSettleGrace = 5.0;

    float tileOrigin(int index) { return MathHelper::TILESIZE * (32 - index); }

    std::string seconds(double value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.2f", value);
        return text;
    }
}

HeightBake::HeightBake(std::shared_ptr<SceneWindow> scene, HApiContainer api, std::string buildDir, std::function<void(const std::string &)> emit) :
    m_scene(std::move(scene)), m_api(std::move(api)), m_buildDir(std::move(buildDir)), m_emit(std::move(emit)) {
}

HeightBake::~HeightBake() = default;

std::shared_ptr<ICamera> HeightBake::camera() const {
    return m_camera;
}

void HeightBake::request(int mapId, int row, int col, double timeoutSeconds, double now) {
    const std::string tile = std::to_string(row) + " " + std::to_string(col);
    if (m_phase != Phase::Idle) {
        m_emit("flyover bake " + tile + " error another tile is in flight");
        return;
    }
    if (row < 0 || row > 63 || col < 0 || col > 63) {
        m_emit("flyover bake " + tile + " error tile out of range");
        return;
    }

    m_mapId = mapId;
    m_row = row;
    m_col = col;
    m_timeout = timeoutSeconds;
    m_started = now;
    m_quietSince = now;
    m_stableFrames = 0;
    m_captured = false;
    m_pending = m_total = 0;
    m_loadSettled = false;

    m_x = tileOrigin(row) - MathHelper::TILESIZE * 0.5f;
    m_y = tileOrigin(col) - MathHelper::TILESIZE * 0.5f;
    m_half = MathHelper::TILESIZE * 128.0f / 127.0f * 0.5f;
    if (!m_camera) m_camera = std::make_shared<BakeCamera>();
    m_camera->place(m_x, m_y, m_half, kTop, kBottom);
    m_scene->setCamera(m_camera);
    m_scene->setBakeView(true, m_x, m_y, m_half);
    m_api->getConfig()->fsrResetRequested = true;
    m_phase = Phase::Waiting;
}

void HeightBake::finish(const std::string &line) {
    m_phase = Phase::Idle;
    m_scene->setBakeView(false, 0, 0, 0);
    m_emit("flyover bake " + std::to_string(m_row) + " " + std::to_string(m_col) + " " + line);
}

void HeightBake::update(double now, const MapRenderPlan *plan) {
    if (m_phase == Phase::Idle) return;

    const bool planIsOurs = plan != nullptr && plan->bake.valid &&
        std::fabs(plan->bake.x - m_x) < 0.01f && std::fabs(plan->bake.y - m_y) < 0.01f;
    if (planIsOurs) {
        m_pending = plan->bake.pending;
        for (int i = 0; i < 3; i++) m_pendingBy[i] = plan->bake.pendingBy[i];
        m_total = plan->bake.total;
        m_loadSettled = plan->loadSettled;
    }

    if (m_phase == Phase::Waiting) {
        if (now - m_started > m_timeout) {
            finish("timeout " + seconds(now - m_started) + " " + std::to_string(m_pending) + "/" + std::to_string(m_total) + " (m2 " + std::to_string(m_pendingBy[0]) + " wmo " + std::to_string(m_pendingBy[1]) + " groups " + std::to_string(m_pendingBy[2]) + ") " +
                   (m_loadSettled ? "settled" : "loading"));
            return;
        }
        const bool quiet = planIsOurs && m_pending == 0;
        if (!quiet) m_quietSince = now;
        m_stableFrames = quiet && (m_loadSettled || now - m_quietSince > kSettleGrace) ? m_stableFrames + 1 : 0;
        if (m_stableFrames < kStableFrames) return;

        m_captured = false;
        if (!m_api->hDevice->captureNextDepth([this](DepthCapture &&capture) {
                m_capture = std::move(capture);
                m_captured = true;
            })) {
            finish("error another depth capture is waiting");
            return;
        }
        m_armed = now;
        m_phase = Phase::Capturing;
        return;
    }

    if (!m_captured) {
        if (now - m_started > m_timeout + 30.0) finish("error the depth capture never came");
        return;
    }

    // The camera is ours until the tile is done, so a plan that is not is a safeguard: wait for loading again.
    if (!planIsOurs) {
        m_stableFrames = 0;
        m_phase = Phase::Waiting;
        return;
    }

    HeightTile tile;
    std::string error;
    if (!reduce(m_capture, tile, error)) {
        finish("error " + error);
        return;
    }

    const std::string mapDir = FarHeights::mapDir(m_buildDir, m_mapId);
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(mapDir), code);

    // The landmarks the bake script named, which it leaves out of the heights, for the far land to draw as themselves. The map's file
    // is written by its first tile, and a tile baked with another list is refused: the tiles already there carry the old one.
    std::vector<int> landmarks;
    {
        const Config &config = *m_api->getConfig();
        std::shared_lock lock(config.landmarks.lock);
        landmarks.assign(config.landmarks.ids.begin(), config.landmarks.ids.end());
    }
    std::sort(landmarks.begin(), landmarks.end());
    if (!std::filesystem::exists(std::filesystem::u8path(FarHeights::landmarksPath(mapDir)), code)) {
        if (!FarHeights::writeLandmarks(mapDir, landmarks, error)) {
            finish("error " + error);
            return;
        }
    } else if (FarHeights::readLandmarks(mapDir) != landmarks) {
        finish("error the script's landmarks differ from " + FarHeights::landmarksPath(mapDir) + "; delete the map's tiles and that file to bake with the new list");
        return;
    }

    if (!FarHeights::writeAtomic(FarHeights::path(mapDir, m_row, m_col), tile, error)) {
        finish("error " + error);
        return;
    }

    // The neighbours in the order north, south, east, west; the edge of this tile that touches each is 0, 1, 3 and 2.
    const int dRow[4] = {-1, 1, 0, 0};
    const int dCol[4] = {0, 0, 1, -1};
    const int edge[4] = {0, 1, 3, 2};
    std::string steps;
    for (int i = 0; i < 4; i++) {
        HeightTile neighbour;
        std::string unused;
        const int r = m_row + dRow[i], c = m_col + dCol[i];
        char text[32] = "-";
        if (r >= 0 && r < 64 && c >= 0 && c < 64 && FarHeights::read(FarHeights::path(mapDir, r, c), neighbour, unused)) {
            std::snprintf(text, sizeof(text), "%.3f", FarHeights::edgeStep(tile, neighbour, edge[i]));
        }
        steps += std::string(" ") + text;
    }
    finish("ok " + seconds(m_armed - m_started) + " " + seconds(now - m_armed) + steps);
}

bool HeightBake::reduce(const DepthCapture &capture, HeightTile &tile, std::string &error) {
    if (!capture.error.empty()) {
        error = capture.error;
        return false;
    }
    if (capture.width != kImage || capture.height != kImage || capture.depth.size() != (size_t) kImage * kImage) {
        error = "the depth image is " + std::to_string(capture.width) + " x " + std::to_string(capture.height) + ", not 256 x 256";
        return false;
    }

    const float x0 = tileOrigin(m_row), y0 = tileOrigin(m_col);
    const float step = MathHelper::TILESIZE / 127.0f;
    float lowest = 1e9f, highest = -1e9f;
    int noData = 0;

    for (int r = 0; r < HeightTile::kSize; r++) {
        for (int c = 0; c < HeightTile::kSize; c++) {
            float depth = 2.0f;
            for (int dr = 0; dr < 2; dr++) {
                for (int dc = 0; dc < 2; dc++) {
                    depth = (std::min)(depth, capture.depth[(2 * r + dr) * kImage + (2 * c + dc)]);
                }
            }

            float z = 0;
            if (depth < kClear) {
                z = m_camera->zOfDepth(depth);
            } else {
                noData++;
                if (!m_scene->wdlGround(x0 - r * step, y0 - c * step, z)) {
                    error = "no depth and no WDL ground at texel " + std::to_string(r) + "," + std::to_string(c);
                    return false;
                }
            }
            const uint16_t value = HeightTile::fromZ(z);
            tile.v[r * HeightTile::kSize + c] = value;
            lowest = (std::min)(lowest, HeightTile::toZ(value));
            highest = (std::max)(highest, HeightTile::toZ(value));
        }
    }
    tile.zMin = lowest;
    tile.zMax = highest;
    if (noData > 0) std::fprintf(stderr, "heightbake %d_%d: %d of 16384 texels took the WDL ground\n", m_row, m_col, noData);
    return true;
}
