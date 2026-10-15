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
#include "farHeights.h"

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
    // The view is the far terrain alone: Config's farTerrainOnly, no tile, model or spawn requested or drawn.
    bool only = false;
    // Config's farTerrainHeights: a tile with baked heights draws on the shared grid.
    bool heights = false;
    // The share of the full scene that shows, from 1 (all of it) to 0 (none): Config's farTerrainShare. Below 1 the full tiles,
    // models and liquids are dissolving into the far land, and the far pass takes the rest of the pixels in the screen-door pattern
    // of Config::m2FadeStartPercent. At 0 with the far terrain alone the full scene's draw is skipped as well.
    float share = 1.0f;
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
        // The tile has a baked height file (FarHeights), whose lowest and highest heights widen aabb. The renderer draws such a tile on
        // the shared grid, displaced by the heights, once they have landed, and on its own range until then.
        bool hasHeights = false;
        float zMin = 0;
        float zMax = 0;
    };

    // The draw range of the grid every baked tile shares: 64 x 64 cells with a skirt, its vertices offsets from the tile's corner in
    // world orientation, at height 0. Empty when no tile has heights.
    struct Grid {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
    };

    // The geometry of every tile and of the grid, built on a worker thread.
    struct Geometry {
        std::vector<FarTerrainVertex> vertices;
        std::vector<uint16_t> indices;
    };

    FarTerrain(const HApiContainer &api, const HWdlFile &wdl, const HWdtFile &wdt, int mapId, const std::string &mapName);

    // Starts the build once the WDL and the WDT have loaded, and takes it in once it is done. True from then on.
    bool poll();

    struct View {
        mathfu::vec3 cameraPos;
        mathfu::mat4 lookAt;
        // The scene's projection as the camera builds it, before the Vulkan fix.
        mathfu::mat4 perspective;
        // Config's farPlaneForCulling.
        float drawDistance = 0;
        // Config's farTerrainShare and farTerrainOnly as the culling pass read them, see FarTerrainFrame.
        float share = 1.0f;
        bool only = false;
    };

    // Fills the frame: the share and the far-alone flag it was given, the far pass's projection, the tiles to draw, and the bounded texture requests.
    void makeFrame(const Config &config, const View &view, const std::vector<ADTObjRenderRes> &adtArray, FarTerrainFrame &frame);

    // Compares the WDL's corner heights of a loaded tile with its ADT's and logs one line, once.
    void checkAdtCorners(const std::vector<ADTObjRenderRes> &adtArray);

    // The WDL's ground under a world position, bilinear in the tile's outer grid. False where the WDL has no tile there.
    bool groundHeight(float x, float y, float &height) const;

    bool isReady() const { return m_ready; }
    const std::vector<Tile> &tiles() const { return m_tiles; }
    const Grid &grid() const { return m_grid; }
    // Reads the baked heights of a tile whose hasHeights is set. False, with the reason on stderr, for a file that no longer reads.
    bool readHeights(size_t tile, HeightTile &out) const;
    // The tile's minimap texture once makeFrame has asked for it, null before. It lives as long as the map does. The frame thread asks
    // and the render thread reads, so both go through a lock.
    HBlpTexture texture(size_t tile) const;
    // The CPU geometry, until the renderer has uploaded it. Null after releaseGeometry.
    const Geometry *geometry() const { return m_geometry.get(); }
    void releaseGeometry() { m_geometry.reset(); }

private:
    struct Built {
        std::unique_ptr<Geometry> geometry;
        std::vector<Tile> tiles;
        Grid grid;
        std::array<int16_t, 64 * 64> tileOfCell;
        float minHeight = 0;
        float maxHeight = 0;
        int minRow = 63, maxRow = 0, minCol = 63, maxCol = 0;
    };
    // heightsDir is the map's folder of baked heights, empty for none.
    static std::unique_ptr<Built> build(const HWdlFile &wdl, const HWdtFile &wdt, const std::string &heightsDir);

    HApiContainer m_api;
    HWdlFile m_wdl;
    HWdtFile m_wdt;
    int m_mapId;
    std::string m_mapName;
    std::string m_heightsDir;
    Grid m_grid;

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

    bool m_cornersChecked = false;
    bool m_reachLogged = false;
};

#endif //WOWVIEWERLIB_FARTERRAIN_H
