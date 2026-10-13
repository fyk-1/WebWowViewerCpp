#ifndef WOWVIEWERLIB_FARTERRAIN_H
#define WOWVIEWERLIB_FARTERRAIN_H

#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <mathfu/glsl_mappings.h>
#include "../../ApiContainer.h"
#include "../../persistance/header/commonFileStructs.h"
#include "../../persistance/wdlFile.h"
#include "../../persistance/wdtFile.h"
#include "../../texture/BlpTexture.h"
#include "../ViewsObjects.h"

class AdtObject;
class Config;

// One vertex of the far terrain: world position, normal and the minimap's uv.
struct FarTerrainVertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(FarTerrainVertex) == 32);

class FarTerrain;

// What the far terrain asks of one frame, made by FarTerrain::makeFrame in the frame plan and read by the renderer.
struct FarTerrainFrame {
    // Config's farTerrain at the time of the plan. Everything below is empty or zero while it is off.
    bool on = false;
    // The view is swapped to the far terrain: above the swap height, or on the way there while the full tiles are still fading out.
    bool swapped = false;
    // The share of the full scene that shows, from 1 (all of it) to 0 (none). Below 1 the full tiles, models and liquids are
    // dissolving into the far land, and the far pass takes the rest of the pixels in the screen-door pattern of Config::m2CullFade.
    // At 0 with the view swapped the full tiles, models and spawns are neither requested nor drawn.
    float fullShare = 1.0f;
    // The far plane of the far pass, in yards.
    float extent = 0;
    // The tiles with a low-detail entry, and how many of their minimap textures have landed.
    int tiles = 0;
    int textures = 0;
    // The tiles to draw, as indices into FarTerrain::tiles, nearest first.
    std::vector<uint16_t> drawn;
    // The far pass's projection times the view's rotation, for Vulkan, without the upscaler's jitter. It is of camera-relative
    // positions: the shader subtracts cameraPos from each vertex first.
    mathfu::mat4 viewProjection = mathfu::mat4::Identity();
    mathfu::vec3 cameraPos = mathfu::vec3(0.0f, 0.0f, 0.0f);
    // The distance the far fog starts at, and its strength.
    float fogStart = 0;
    float fogStrength = 0;
    // Keeps the geometry and the textures alive for the renderer, which reads them by tile index.
    std::shared_ptr<FarTerrain> terrain;
};

/*
 * The low-detail ring of terrain beyond the draw distance: the map's WDL heights, one tile of 16 x 16 cells for each entry of
 * its MAOF chunk, wearing the minimap tile of the same name. The map owns one per open map. The geometry is built once on a
 * worker thread when the WDL has loaded, and the renderer uploads it as a whole.
 */
class FarTerrain : public std::enable_shared_from_this<FarTerrain> {
public:
    struct Tile {
        // The tile's place on the map: row along world x (falling), column along world y (falling).
        uint8_t row = 0;
        uint8_t col = 0;
        // The tile's draw range in the geometry: indices from firstIndex (local to the tile), added to vertexOffset.
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        CAaBox aabb;
        // The FileDataID of the tile's minimap BLP, 0 where the map has none.
        uint32_t minimapFileDataId = 0;
    };

    // The geometry of every tile, built on a worker thread.
    struct Geometry {
        std::vector<FarTerrainVertex> vertices;
        std::vector<uint16_t> indices;
    };

    FarTerrain(const HApiContainer &api, const HWdlFile &wdl, const HWdtFile &wdt, int mapId, const std::string &mapName);

    // Starts the build once the WDL and the WDT have loaded, and takes it in once it is done. True from then on.
    bool poll();

    // Decides whether the view is swapped to the far terrain, from the camera's height over the WDL's ground, with a hysteresis
    // band, and moves the share of the full scene that shows toward 0 when it is and toward 1 when it is not, by the frame's
    // time in milliseconds. Config's farTerrainSwapHeight of 0 never swaps. True while the full scene is gone altogether, which
    // is when the map stops asking for tiles, models and spawns.
    bool updateSwap(const Config &config, const mathfu::vec3 &cameraPos, float deltaMs);

    struct View {
        mathfu::vec3 cameraPos;
        mathfu::mat4 lookAt;
        // The scene's projection as the camera builds it, before the Vulkan fix.
        mathfu::mat4 perspective;
        // Config's effectiveFarPlaneForCulling().
        float drawDistance = 0;
    };

    // Fills the frame: the swap, the far pass's projection, the tiles to draw, and the bounded texture requests.
    void makeFrame(const Config &config, const View &view, const std::vector<ADTObjRenderRes> &adtArray, FarTerrainFrame &frame);

    // Compares the WDL's corner heights of a loaded tile with its ADT's and logs one line, once.
    void checkAdtCorners(const std::vector<ADTObjRenderRes> &adtArray);

    // The WDL's ground under a world position, bilinear in the tile's outer grid. False where the WDL has no tile there.
    bool groundHeight(float x, float y, float &height) const;

    bool isReady() const { return m_ready; }
    const std::vector<Tile> &tiles() const { return m_tiles; }
    // The tile's minimap texture once makeFrame has asked for it, null before. It lives as long as the map does. The frame thread asks
    // and the render thread reads, so both go through a lock.
    HBlpTexture texture(size_t tile) const;
    // The CPU geometry, until the renderer has uploaded it. Null after releaseGeometry.
    const Geometry *geometry() const { return m_geometry.get(); }
    void releaseGeometry() { m_geometry.reset(); }

    bool isSwapped() const { return m_swapped; }
    float fullShare() const { return m_fullShare; }

private:
    struct Built {
        std::unique_ptr<Geometry> geometry;
        std::vector<Tile> tiles;
        std::array<int16_t, 64 * 64> tileOfCell;
        float minHeight = 0;
        float maxHeight = 0;
        int minRow = 63, maxRow = 0, minCol = 63, maxCol = 0;
    };
    static std::unique_ptr<Built> build(const HWdlFile &wdl, const HWdtFile &wdt);

    HApiContainer m_api;
    HWdlFile m_wdl;
    HWdtFile m_wdt;
    int m_mapId;
    std::string m_mapName;

    std::future<std::unique_ptr<Built>> m_build;
    bool m_buildStarted = false;
    bool m_ready = false;

    std::unique_ptr<Geometry> m_geometry;
    std::vector<Tile> m_tiles;
    mutable std::mutex m_textureMutex;
    std::vector<HBlpTexture> m_textures;
    std::array<int16_t, 64 * 64> m_tileOfCell;
    float m_minHeight = 0;
    float m_maxHeight = 0;
    int m_minRow = 63, m_maxRow = 0, m_minCol = 63, m_maxCol = 0;

    bool m_swapped = false;
    // The share of the full scene that shows, see FarTerrainFrame::fullShare. Set from the first swap decision.
    float m_fullShare = 1.0f;
    bool m_shareStarted = false;
    // Set while a tile the plan asked for has not loaded, and the time the share has waited on it, in milliseconds.
    bool m_adtPending = false;
    float m_pendingMs = 0.0f;
    bool m_cornersChecked = false;
    bool m_reachLogged = false;
};

#endif //WOWVIEWERLIB_FARTERRAIN_H
