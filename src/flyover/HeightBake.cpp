#include "HeightBake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include "../ui/childWindow/sceneWindow/SceneWindow.h"
#include "../../wowViewerLib/src/engine/algorithms/mathHelper.h"
#include "../../wowViewerLib/src/engine/camera/bakeCamera.h"
#include "../../wowViewerLib/src/engine/persistance/adtFile.h"
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

The frame's colour is read back with its depth (IDevice::captureNextFrame, armed on the same frame) and reduced to the colour tile on
the same lattice. The bake script sets Config::bakeUnlit and disableFog, so the pixels are the ground's albedo and not a lit picture.
A colour texel is the mean of the drawn pixels of its 2 x 2 block, or of the tile's drawn pixels when the block has none.

The colour texel's alpha is the liquid kind, read from the tile's root ADT and never from the render, so the liquid stays out of the
heights. The ADT comes from the cache the scene uses (its FileDataID from the WDT), and the tile waits for it to load before the captures
are armed. The kind grid is 128 x 128, one entry for each liquid cell (8 x 8 to a chunk, 16 x 16 chunks), and MH2O is walked as
AdtObject::loadWater walks it. A cell is placed by its world position: the centre x, y that LiquidInstance builds its vertices from, taken
to the cell of the lattice in `reduce`'s frame. Colour sample i lies at i / 127 of the tile and cell k spans k / 128 to (k + 1) / 128, so a
sample takes cell i * 128 / 127, held to 127. The kinds are 0 none, 1 river or lake, 2 ocean, 3 magma, 4 other. An instance's kind comes
from its LiquidType (the LiquidObject's, when the instance names one): ocean for type 2, else the type's material decides, 1 or 3
water except type 4, 2 or 4 magma, the rest other. Where layers overlap the higher kind stays.
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
    m_serial++;
    m_captured = false;
    m_colorCaptured = false;
    m_pending = m_total = 0;
    m_loadSettled = false;

    m_adt = nullptr;
    if (const uint32_t rootAdt = m_scene->rootAdtFileDataId(row, col); rootAdt != 0) {
        m_adt = m_api->cacheStorage->getAdtGeomCache()->getFileId((int) rootAdt);
        // As AdtObject does, so a tile the scene makes from the same file still reads its chunks.
        m_adt->setIsMain(true);
    }

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
    m_adt = nullptr;
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
        if (m_adt != nullptr && m_adt->getStatus() == FileStatus::FSRejected) {
            finish("error the ADT did not load");
            return;
        }
        const bool quiet = planIsOurs && m_pending == 0;
        if (!quiet) m_quietSince = now;
        m_stableFrames = quiet && (m_loadSettled || now - m_quietSince > kSettleGrace) ? m_stableFrames + 1 : 0;
        if (m_stableFrames < kStableFrames) return;
        if (m_adt != nullptr && m_adt->getStatus() != FileStatus::FSLoaded) return;

        m_captured = false;
        m_colorCaptured = false;
        if (!m_api->hDevice->captureNextDepth([this, serial = m_serial](DepthCapture &&capture) {
                if (serial != m_serial) return;
                m_capture = std::move(capture);
                m_captured = true;
            })) {
            finish("error another depth capture is waiting");
            return;
        }
        if (!m_api->hDevice->captureNextFrame([this, serial = m_serial](FrameCapture &&capture) {
                if (serial != m_serial) return;
                m_colorCapture = std::move(capture);
                m_colorCaptured = true;
            })) {
            finish("error another frame capture is waiting");
            return;
        }
        m_armed = now;
        m_phase = Phase::Capturing;
        return;
    }

    if (!m_captured || !m_colorCaptured) {
        if (now - m_started > m_timeout + 30.0) finish("error the depth or frame capture never came");
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
    ColorTile color;
    bool colorDrawn = false;
    buildLiquidKinds();
    if (!reduceColor(m_colorCapture, m_capture, color, colorDrawn, error)) {
        finish("error " + error);
        return;
    }

    const std::string mapDir = FarHeights::mapDir(m_buildDir, m_mapId);
    std::error_code code;
    std::filesystem::create_directories(std::filesystem::path(mapDir), code);

    // The landmarks the bake script named, which it leaves out of the heights, for the far land to draw as themselves. The map's file
    // is written by its first tile, and a tile baked with another list is refused: the tiles already there carry the old one.
    std::vector<IdEntry> landmarks;
    {
        const Config &config = *m_api->getConfig();
        std::shared_lock lock(config.landmarks.lock);
        landmarks = config.landmarks.entries();
    }
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
    if (colorDrawn && !FarHeights::writeColorAtomic(FarHeights::colorPath(mapDir, m_row, m_col), color, error)) {
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

void HeightBake::buildLiquidKinds() {
    constexpr int kCells = ColorTile::kSize;
    m_kinds.assign((size_t) kCells * kCells, 0);
    if (m_adt == nullptr || m_adt->mH2OHeader == nullptr || (size_t) m_adt->mH2OBlob_len < sizeof(M2HOHeader)) return;

    // One database lookup for each distinct (LiquidObject, LiquidType) pair of the tile.
    std::map<uint32_t, uint8_t> kindOfPair;
    auto kindOf = [&](const SMLiquidInstance &instance) -> uint8_t {
        const uint32_t key = ((uint32_t) instance.liquid_object_or_lvf << 16) | instance.liquid_type;
        if (auto found = kindOfPair.find(key); found != kindOfPair.end()) return found->second;

        int type = instance.liquid_type;
        if (instance.liquid_object_or_lvf >= 42) {
            LiquidObjectRec object;
            object.liquid_object_or_lvf = instance.liquid_object_or_lvf;
            object.liquidTypeId = type;
            m_api->databaseHandler->getLiquidObjectData(instance.liquid_object_or_lvf, type, object);
            type = object.liquidTypeId;
        }

        uint8_t kind = 4;
        if (type == 2) {
            kind = 2;
        } else {
            LiquidTypeAndMat material{};
            std::vector<LiquidTextureData> textures;
            m_api->databaseHandler->getLiquidTypeData(type, material, textures);
            if (material.materialID == 2 || material.materialID == 4) kind = 3;
            else if ((material.materialID == 1 || material.materialID == 3) && type != 4) kind = 1;
        }
        kindOfPair[key] = kind;
        return kind;
    };

    const float x0 = tileOrigin(m_row), y0 = tileOrigin(m_col);
    const float cellSize = MathHelper::TILESIZE / kCells;
    const size_t blobLength = (size_t) m_adt->mH2OBlob_len;
    const char *blob = &m_adt->mH2OBlob[0];

    for (int yChunk = 0; yChunk < 16; yChunk++) {
        for (int xChunk = 0; xChunk < 16; xChunk++) {
            const M2HOHeader::SMLiquidChunk &chunk = m_adt->mH2OHeader->chunks[yChunk * 16 + xChunk];
            if (chunk.layer_count == 0) continue;
            if ((size_t) chunk.offset_instances + (size_t) chunk.layer_count * sizeof(SMLiquidInstance) > blobLength) continue;

            // The chunk's base as AdtObject::loadWater makes it: x falls along the chunk's y index, y along its x.
            const float baseX = x0 - MathHelper::CHUNKSIZE * yChunk;
            const float baseY = y0 - MathHelper::CHUNKSIZE * xChunk;

            for (uint32_t layer = 0; layer < chunk.layer_count; layer++) {
                SMLiquidInstance instance;
                std::memcpy(&instance, blob + chunk.offset_instances + layer * sizeof(SMLiquidInstance), sizeof(instance));

                const bool object = instance.liquid_object_or_lvf >= 42;
                const int xOffset = object ? 0 : instance.x_offset, yOffset = object ? 0 : instance.y_offset;
                const int width = object ? 8 : instance.width, height = object ? 8 : instance.height;
                if (width <= 0 || height <= 0) continue;

                const uint8_t *exists = nullptr;
                if (instance.offset_exists_bitmap != 0) {
                    if ((size_t) instance.offset_exists_bitmap + (size_t) (width * height + 7) / 8 > blobLength) continue;
                    exists = (const uint8_t *) blob + instance.offset_exists_bitmap;
                }
                const uint8_t kind = kindOf(instance);

                for (int cy = 0; cy < height; cy++) {
                    for (int cx = 0; cx < width; cx++) {
                        const int bit = cy * width + cx;
                        if (exists != nullptr && ((exists[bit >> 3] >> (bit & 7)) & 1) == 0) continue;

                        // The cell's centre, midway between its four vertices.
                        const float x = baseX - MathHelper::UNITSIZE * (cy + yOffset + 0.5f);
                        const float y = baseY - MathHelper::UNITSIZE * (cx + xOffset + 0.5f);
                        const int r = std::clamp((int) std::floor((x0 - x) / cellSize), 0, kCells - 1);
                        const int c = std::clamp((int) std::floor((y0 - y) / cellSize), 0, kCells - 1);
                        uint8_t &held = m_kinds[(size_t) r * kCells + c];
                        held = (std::max)(held, kind);
                    }
                }
            }
        }
    }
}

bool HeightBake::reduceColor(const FrameCapture &capture, const DepthCapture &depth, ColorTile &tile, bool &drawn, std::string &error) {
    drawn = false;
    if (!capture.error.empty()) {
        error = capture.error;
        return false;
    }
    if (capture.width != kImage || capture.height != kImage || capture.rgb.size() != (size_t) kImage * kImage * 3) {
        error = "the colour image is " + std::to_string(capture.width) + " x " + std::to_string(capture.height) + ", not 256 x 256";
        return false;
    }
    if (depth.depth.size() != (size_t) kImage * kImage) {
        error = "the depth image is not 256 x 256";
        return false;
    }

    constexpr int kTexels = ColorTile::kSize * ColorTile::kSize;
    std::vector<int> sum(kTexels * 3, 0), count(kTexels, 0);
    long long tileSum[3] = {0, 0, 0};
    long long tileCount = 0;

    for (int y = 0; y < kImage; y++) {
        for (int x = 0; x < kImage; x++) {
            const size_t pixel = (size_t) y * kImage + x;
            if (depth.depth[pixel] >= kClear) continue;

            const int texel = (y / 2) * ColorTile::kSize + (x / 2);
            count[texel]++;
            tileCount++;
            for (int i = 0; i < 3; i++) {
                const int value = capture.rgb[pixel * 3 + i];
                sum[texel * 3 + i] += value;
                tileSum[i] += value;
            }
        }
    }
    if (tileCount == 0) {
        std::fprintf(stderr, "heightbake %d_%d: nothing drawn, no colour file\n", m_row, m_col);
        return true;
    }

    for (int texel = 0; texel < kTexels; texel++) {
        for (int i = 0; i < 3; i++) {
            const long long total = count[texel] > 0 ? sum[texel * 3 + i] : tileSum[i];
            const long long n = count[texel] > 0 ? count[texel] : tileCount;
            tile.rgba[texel * 4 + i] = (uint8_t) ((total + n / 2) / n);
        }
        const int r = texel / ColorTile::kSize, c = texel % ColorTile::kSize;
        const int cell = (std::min)(ColorTile::kSize - 1, r * ColorTile::kSize / (ColorTile::kSize - 1)) * ColorTile::kSize +
                         (std::min)(ColorTile::kSize - 1, c * ColorTile::kSize / (ColorTile::kSize - 1));
        tile.rgba[texel * 4 + 3] = m_kinds[cell];
    }
    drawn = true;
    return true;
}
