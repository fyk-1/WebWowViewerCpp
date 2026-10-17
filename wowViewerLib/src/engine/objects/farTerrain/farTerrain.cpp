#include "farTerrain.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <iomanip>
#include <sstream>
#include "../../algorithms/mathHelper.h"
#include "../adt/adtObject.h"
#include "farLandmarks.h"

namespace {
    constexpr float kTileSize = MathHelper::TILESIZE;
    constexpr float kCellSize = MathHelper::TILESIZE / 16.0f;

    /*
    robots@Impl: The skirt.

    Each tile hangs a strip of quads below its four edges, so a crack between two tiles, or between a tile and the full terrain that
    meets it, shows the strip and not the sky behind. The WDL's outer vertices are the ADT's chunk corners, so the heights agree at
    every corner, and between corners the ADT's own surface differs from the straight line by a few yards at most. The strip drops
    32 yards, a margin of several times that, and not more, since a long skirt is seen edge-on from the air as a wall. The same strip
    covers the edge between two baked tiles drawn on grids of different levels, where the coarser edge cuts straight between samples
    the finer one follows. A building taller than the strip that straddles such an edge leaves a crack, and the strip is kept short
    anyway: such an edge lies Config::farGridHalfDistance off or more, where the crack is a few pixels.
    */
    constexpr float kSkirtDepth = 32.0f;

    /*
    robots@Impl: The near plane of the far pass.

    A quarter of the draw distance. The full terrain reaches the draw distance along the view axis, so the part of a far tile
    that the near plane clips lies where the full chunks are drawn, and a quarter leaves a wide margin for the tiles still loading
    when the camera arrives, which show far terrain from there on and nothing nearer. A larger fraction would open a hole in front
    of the camera while they load, a smaller one only wastes depth precision on the far tiles.
    */
    constexpr float kNearFraction = 0.25f;

    /*
    robots@Impl: The far land's band of depth.

    The scene's depth is not reversed: it clears to 1 and compares less-or-equal, and every scene fragment is drawn through the
    usual viewport, whose window depth runs 0 to 0.99, so however far a fragment lies it ends below 0.99, and the shaping of
    the projection only decides how crowded that range is near its top. The sky draws at the skybox viewport's 0.997 to 1 with no
    depth write. Between the two lies the map-area viewport, 0.991 to 0.996, which nothing else draws through. The far pass
    draws through it (vp_mapArea), with its projection mapping the near-to-far range onto NDC 0 to 1, linearly in 1/distance
    like any perspective depth, so the far tiles order among themselves in a band that no scene fragment can reach. A far
    fragment passes the depth test only against a pixel still at the clear value, or holding sky, and writes depth short
    of the sky's 0.997, so the sky loses to it. Where the scene drew anything the far land loses, at any distance. The band is
    0.005 wide, and the depth buffer, 32-bit float or 24-bit fixed alike (the engine takes D32 with stencil first), resolves 2^-24
    in it, about 84 000 steps, which is every step the far land has to order itself by. Folding the mapping into the projection
    keeps the interpolation exact, since the clip z stays an affine function of the vertex.
    */

    /*
    robots@Impl: The near plane of the depth.

    Depth is linear in 1/distance, so the steps crowd at the near plane: the share of the band beyond a distance d is
    (1/n - 1/d) / (1/n - 1/extent) of it for a near plane n, and a near plane at the scene's 1 yard puts every tile past
    1 900 yards into the last 0.05 % of the band, about 40 steps for the whole of 1 900 to 16 000 yards. Tiles a thousand yards
    apart then tie, and a tie goes to the tile drawn later, which is the farther one, since the tiles go nearest first; the
    ridges of the far land swap places as the camera moves. The near plane that spends the steps where the land is is the nearest
    the far land that shows can be. The view-space depth of a point in the frustum is at least its distance times the cosine of
    the frustum's corner ray, and a tile's box is no nearer to the camera than its geometry, so the least box distance of the
    tiles in view times that cosine bounds every visible depth from below, and nothing visible is clipped. kDepthNearMargin
    is the slack for the float arithmetic of the planes and for the upscaler's jitter. At 780 yards the steps are about a
    twentieth of a yard at 2 000 yards and under 4 at the extent, well under the skirt and the cells. Where the view is neither
    alone nor fading the near plane stays a quarter of the draw distance (kNearFraction), which is the nearest a tile still
    loading can be there.
    */
    constexpr float kDepthNearMargin = 0.95f;

    constexpr int kTexturesPerFrame = 4;

    float sampleHeight(const WdlFile &wdl, const WdlFile::Tile &own, int row, int col, int r, int c) {
        int tileRow = row, tileCol = col, nr = r, nc = c;
        if (nr < 0) { tileRow--; nr += 16; } else if (nr > 16) { tileRow++; nr -= 16; }
        if (nc < 0) { tileCol--; nc += 16; } else if (nc > 16) { tileCol++; nc -= 16; }

        if (tileRow != row || tileCol != col) {
            WdlFile::Tile neighbour;
            if (wdl.getTile(tileRow, tileCol, neighbour)) {
                return neighbour.outer[nr * 17 + nc];
            }
        }
        return own.outer[std::clamp(r, 0, 16) * 17 + std::clamp(c, 0, 16)];
    }

    bool isHole(const WdlFile::Tile &tile, int r, int c) {
        return tile.holes != nullptr && ((tile.holes[r] >> c) & 1) != 0;
    }

    // The skirt of a grid of n x n cells whose (n + 1)^2 vertices were just appended at vertexOffset, row-major: a second row of
    // vertices under each edge, and a quad between the two rows for every cell on the edge that isHole(row, col) does not name.
    // The vertex positions are in world orientation, so the quads wind away from the grid.
    template <typename Hole>
    void appendSkirt(FarTerrain::Geometry &geometry, int32_t vertexOffset, int n, Hole isHole) {
        auto grid = [n](int r, int c) -> uint16_t { return (uint16_t) (r * (n + 1) + c); };

        struct Edge { int r0, c0, dr, dc, outwardX, outwardY; };
        const Edge edges[4] = {
            {0, 0, 0, 1, 1, 0},
            {n, 0, 0, 1, -1, 0},
            {0, 0, 1, 0, 0, 1},
            {0, n, 1, 0, 0, -1},
        };
        for (const Edge &edge : edges) {
            const size_t bottomStart = geometry.vertices.size();
            for (int k = 0; k <= n; k++) {
                FarTerrainVertex bottom = geometry.vertices[vertexOffset + grid(edge.r0 + k * edge.dr, edge.c0 + k * edge.dc)];
                bottom.position[2] -= kSkirtDepth;
                geometry.vertices.push_back(bottom);
            }
            for (int k = 0; k < n; k++) {
                const int cellRow = edge.dr == 0 ? (edge.r0 == 0 ? 0 : n - 1) : k;
                const int cellCol = edge.dc == 0 ? (edge.c0 == 0 ? 0 : n - 1) : k;
                if (isHole(cellRow, cellCol)) continue;

                const uint16_t t0 = grid(edge.r0 + k * edge.dr, edge.c0 + k * edge.dc);
                const uint16_t t1 = grid(edge.r0 + (k + 1) * edge.dr, edge.c0 + (k + 1) * edge.dc);
                const uint16_t b0 = (uint16_t) (bottomStart - vertexOffset + k);
                const uint16_t b1 = (uint16_t) (b0 + 1);

                // Wound so that the quad faces away from the tile, whatever order the edge runs in.
                const auto &p0 = geometry.vertices[vertexOffset + t0].position;
                const auto &p1 = geometry.vertices[vertexOffset + t1].position;
                const auto &q0 = geometry.vertices[vertexOffset + b0].position;
                const float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
                const float vx = q0[0] - p0[0], vy = q0[1] - p0[1], vz = q0[2] - p0[2];
                const float nx = uy * vz - uz * vy;
                const float ny = uz * vx - ux * vz;
                const bool outward = nx * edge.outwardX + ny * edge.outwardY > 0.0f;
                if (outward) {
                    geometry.indices.insert(geometry.indices.end(), {t0, t1, b0, t1, b1, b0});
                } else {
                    geometry.indices.insert(geometry.indices.end(), {t0, b0, t1, t1, b0, b1});
                }
            }
        }
    }

    // The cells of the grid a baked tile is drawn on: its vertices are offsets from the tile's corner, in world orientation (the
    // vertex shader adds them to the corner), and the baked heights displace them.
    constexpr int kGridCells = 64;
}

FarTerrain::FarTerrain(const HApiContainer &api, const HWdlFile &wdl, const HWdtFile &wdt, int mapId, const std::string &mapName) :
    m_api(api), m_wdl(wdl), m_wdt(wdt), m_mapId(mapId), m_mapName(mapName) {
    m_tileOfCell.fill(-1);
    const std::string &heightsRoot = api->getConfig()->farHeightsDir;
    if (!heightsRoot.empty()) m_heightsDir = FarHeights::mapDir(heightsRoot, mapId);
}

std::unique_ptr<FarTerrain::Built> FarTerrain::build(const HWdlFile &wdl, const HWdtFile &wdt, const std::string &heightsDir) {
    auto built = std::make_unique<Built>();
    built->geometry = std::make_unique<Geometry>();
    built->tileOfCell.fill(-1);
    auto &geometry = *built->geometry;

    const bool hasMaid = wdt->mphd != nullptr && wdt->mphd->flags.wdt_has_maid && wdt->mapFileDataIDs != nullptr;
    bool anyHeight = false;
    bool anyHeights = false;

    for (int row = 0; row < 64; row++) {
        for (int col = 0; col < 64; col++) {
            WdlFile::Tile source;
            if (!wdl->getTile(row, col, source)) continue;

            Tile tile;
            tile.row = row;
            tile.col = col;
            tile.vertexOffset = (int32_t) geometry.vertices.size();
            tile.firstIndex = (uint32_t) geometry.indices.size();
            tile.minimapFileDataId = hasMaid ? wdt->mapFileDataIDs->array[row * 64 + col].minimapTexture : 0;

            const float x0 = AdtIndexToWorldCoordinate((float) row);
            const float y0 = AdtIndexToWorldCoordinate((float) col);

            float minZ = 1e9f, maxZ = -1e9f;
            for (int r = 0; r <= 16; r++) {
                for (int c = 0; c <= 16; c++) {
                    const float z = source.outer[r * 17 + c];
                    minZ = std::min(minZ, z);
                    maxZ = std::max(maxZ, z);

                    const float dr = (sampleHeight(*wdl, source, row, col, r + 1, c) - sampleHeight(*wdl, source, row, col, r - 1, c)) / (2.0f * kCellSize);
                    const float dc = (sampleHeight(*wdl, source, row, col, r, c + 1) - sampleHeight(*wdl, source, row, col, r, c - 1)) / (2.0f * kCellSize);
                    const float invLength = 1.0f / std::sqrt(dr * dr + dc * dc + 1.0f);

                    FarTerrainVertex &vertex = geometry.vertices.emplace_back();
                    vertex.position[0] = x0 - r * kCellSize;
                    vertex.position[1] = y0 - c * kCellSize;
                    vertex.position[2] = z;
                    vertex.normal[0] = dr * invLength;
                    vertex.normal[1] = dc * invLength;
                    vertex.normal[2] = invLength;
                    vertex.uv[0] = c / 16.0f;
                    vertex.uv[1] = r / 16.0f;
                }
            }

            // Each cell is two triangles, counter-clockwise seen from above, since x and y fall along r and c.
            auto grid = [](int r, int c) -> uint16_t { return (uint16_t) (r * 17 + c); };
            for (int r = 0; r < 16; r++) {
                for (int c = 0; c < 16; c++) {
                    if (isHole(source, r, c)) continue;
                    const uint16_t a = grid(r, c), b = grid(r, c + 1), d = grid(r + 1, c), e = grid(r + 1, c + 1);
                    geometry.indices.insert(geometry.indices.end(), {a, d, b, b, d, e});
                }
            }

            appendSkirt(geometry, tile.vertexOffset, 16, [&](int r, int c) { return isHole(source, r, c); });

            tile.indexCount = (uint32_t) geometry.indices.size() - tile.firstIndex;
            // A baked tile's box takes in its baked heights, which stand above the WDL's wherever a building does.
            if (!heightsDir.empty() && FarHeights::readHeader(FarHeights::path(heightsDir, row, col), tile.zMin, tile.zMax)) {
                tile.hasHeights = true;
                minZ = std::min(minZ, tile.zMin);
                maxZ = std::max(maxZ, tile.zMax);
                anyHeights = true;
            }
            if (!heightsDir.empty()) {
                std::error_code code;
                const auto bytes = std::filesystem::file_size(std::filesystem::u8path(FarHeights::colorPath(heightsDir, row, col)), code);
                tile.hasColor = !code && bytes == FarHeights::kColorFileBytes;
            }
            tile.aabb = CAaBox(
                C3Vector(mathfu::vec3(x0 - kTileSize, y0 - kTileSize, minZ - kSkirtDepth)),
                C3Vector(mathfu::vec3(x0, y0, maxZ))
            );

            built->tileOfCell[row * 64 + col] = (int16_t) built->tiles.size();
            built->tiles.push_back(tile);

            built->minRow = std::min(built->minRow, row);
            built->maxRow = std::max(built->maxRow, row);
            built->minCol = std::min(built->minCol, col);
            built->maxCol = std::max(built->maxCol, col);
            if (!anyHeight) {
                built->minHeight = minZ;
                built->maxHeight = maxZ;
                anyHeight = true;
            } else {
                built->minHeight = std::min(built->minHeight, minZ);
                built->maxHeight = std::max(built->maxHeight, maxZ);
            }
        }
    }

    // The grids the baked tiles share, after every tile's own geometry, one for each level.
    for (int level = 0; anyHeights && level < kGridLevels; level++) {
        Grid &range = built->grids[level];
        range.vertexOffset = (int32_t) geometry.vertices.size();
        range.firstIndex = (uint32_t) geometry.indices.size();

        const int cells = kGridCells >> level;
        const float cell = kTileSize / (float) cells;
        for (int r = 0; r <= cells; r++) {
            for (int c = 0; c <= cells; c++) {
                FarTerrainVertex &vertex = geometry.vertices.emplace_back();
                vertex.position[0] = -r * cell;
                vertex.position[1] = -c * cell;
                vertex.position[2] = 0.0f;
                vertex.normal[0] = 0.0f;
                vertex.normal[1] = 0.0f;
                vertex.normal[2] = 1.0f;
                vertex.uv[0] = c / (float) cells;
                vertex.uv[1] = r / (float) cells;
            }
        }
        auto grid = [cells](int r, int c) -> uint16_t { return (uint16_t) (r * (cells + 1) + c); };
        for (int r = 0; r < cells; r++) {
            for (int c = 0; c < cells; c++) {
                const uint16_t a = grid(r, c), b = grid(r, c + 1), d = grid(r + 1, c), e = grid(r + 1, c + 1);
                geometry.indices.insert(geometry.indices.end(), {a, d, b, b, d, e});
            }
        }
        appendSkirt(geometry, range.vertexOffset, cells, [](int, int) { return false; });
        range.indexCount = (uint32_t) geometry.indices.size() - range.firstIndex;
    }

    return built;
}

bool FarTerrain::poll() {
    if (m_ready) return true;

    if (!m_buildStarted) {
        if (m_wdl == nullptr || m_wdt == nullptr) return false;
        if (m_wdl->getStatus() != FileStatus::FSLoaded || m_wdt->getStatus() != FileStatus::FSLoaded) return false;

        m_buildStarted = true;
        m_build = std::async(std::launch::async, [wdl = m_wdl, wdt = m_wdt, heightsDir = m_heightsDir]() { return build(wdl, wdt, heightsDir); });
        return false;
    }

    if (m_build.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

    auto built = m_build.get();
    m_geometry = std::move(built->geometry);
    m_tiles = std::move(built->tiles);
    m_grids = built->grids;
    m_textures.assign(m_tiles.size(), nullptr);
    m_colorFailed.assign(m_tiles.size(), 0);
    m_tileOfCell = built->tileOfCell;
    m_minHeight = built->minHeight;
    m_maxHeight = built->maxHeight;
    m_minRow = built->minRow;
    m_maxRow = built->maxRow;
    m_minCol = built->minCol;
    m_maxCol = built->maxCol;
    m_ready = true;

    size_t baked = 0, coloured = 0;
    for (const auto &tile : m_tiles) {
        baked += tile.hasHeights ? 1 : 0;
        coloured += tile.hasColor ? 1 : 0;
    }
    std::cerr << "far terrain: " << m_tiles.size() << " tiles, " << baked << " with baked heights, " << coloured << " with baked colour, " << m_geometry->vertices.size() << " vertices, "
              << m_geometry->indices.size() / 3 << " triangles built for map " << m_mapId << std::endl;
    return true;
}

bool FarTerrain::readHeights(size_t tile, HeightTile &out) const {
    if (tile >= m_tiles.size() || !m_tiles[tile].hasHeights) return false;

    const std::string file = FarHeights::path(m_heightsDir, m_tiles[tile].row, m_tiles[tile].col);
    std::string error;
    if (FarHeights::read(file, out, error)) return true;

    std::cerr << "far terrain: " << error << std::endl;
    return false;
}

bool FarTerrain::readColor(size_t tile, ColorTile &out) const {
    if (tile >= m_tiles.size() || !m_tiles[tile].hasColor) return false;
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        if (m_colorFailed[tile]) return false;
    }

    const std::string file = FarHeights::colorPath(m_heightsDir, m_tiles[tile].row, m_tiles[tile].col);
    std::string error;
    if (FarHeights::readColor(file, out, error)) return true;

    std::cerr << "far terrain: " << error << std::endl;
    std::lock_guard<std::mutex> lock(m_textureMutex);
    m_colorFailed[tile] = 1;
    return false;
}

HBlpTexture FarTerrain::texture(size_t tile) const {
    std::lock_guard<std::mutex> lock(m_textureMutex);
    return tile < m_textures.size() ? m_textures[tile] : nullptr;
}

bool FarTerrain::groundHeight(float x, float y, float &height) const {
    if (m_wdl == nullptr || m_wdl->getStatus() != FileStatus::FSLoaded) return false;

    const float rowF = 32.0f - x / kTileSize;
    const float colF = 32.0f - y / kTileSize;
    const int row = (int) std::floor(rowF);
    const int col = (int) std::floor(colF);

    WdlFile::Tile tile;
    if (!m_wdl->getTile(row, col, tile)) return false;

    const float r = (rowF - row) * 16.0f;
    const float c = (colF - col) * 16.0f;
    const int r0 = std::min((int) r, 15);
    const int c0 = std::min((int) c, 15);
    const float u = r - r0;
    const float v = c - c0;

    const float h00 = tile.outer[r0 * 17 + c0], h01 = tile.outer[r0 * 17 + c0 + 1];
    const float h10 = tile.outer[(r0 + 1) * 17 + c0], h11 = tile.outer[(r0 + 1) * 17 + c0 + 1];
    height = (h00 * (1 - v) + h01 * v) * (1 - u) + (h10 * (1 - v) + h11 * v) * u;
    return true;
}

/*
robots@Impl: The far pass's projection and its tiles.

The view is the scene's. The projection is built from the scene's own (its fov and aspect), with the near plane at a quarter of the
draw distance, or while the view is alone or fading at the nearest the visible far land can be (see the near plane of the depth
above), and the far plane at the map's extent: the greatest distance from the camera to the rectangle of tiles with a low-detail
entry, taken to its four corners at the heights that lie furthest in z. The depth range is folded into the matrix (see the band of
depth above), and Vulkan's flipped y with it. The matrix holds the view's rotation alone and the shader takes the camera off each
vertex first, so that the world coordinates of the far land, up to 17 000 yards, never meet the camera's translation in a float.

The tiles are culled by their boxes against the planes of the same projection as the camera builds it. A tile whose ADT has loaded
is left out, unless the full scene is fading, since the full terrain draws it, whichever of its chunks the frustum keeps this frame: the far land would otherwise
draw inside it. The one exception is a loaded tile that reaches past the draw distance along the view axis, where the full chunks
are culled by the far plane and the far land has to carry on from the edge; it stays, and loses to the full terrain wherever that
draws (see the band of depth above). A tile whose ADT has not loaded stays, and so does a loaded one the full terrain left out of
the plan altogether. The tiles to draw go nearest first, so the early depth test rejects the most behind them.
*/
void FarTerrain::makeFrame(const Config &config, const View &view, const std::vector<ADTObjRenderRes> &adtArray, FarTerrainFrame &frame) {
    frame.on = true;
    frame.heights = config.farTerrainHeights;
    frame.drawn.clear();
    frame.levels.clear();
    frame.terrain = shared_from_this();

    // The landmarks the bake recorded beside the heights (FarHeights::readLandmarks), read once; the frame carries them once built.
    if (!m_landmarksRead) {
        m_landmarksRead = true;
        const std::vector<IdEntry> ids = m_heightsDir.empty() ? std::vector<IdEntry>() : FarHeights::readLandmarks(m_heightsDir);
        if (!ids.empty()) m_landmarks = std::make_shared<FarLandmarks>(m_api, m_wdt, m_mapId, ids);
    }
    frame.landmarks = m_landmarks != nullptr && m_landmarks->poll() ? m_landmarks : nullptr;
    frame.fogStart = view.drawDistance;
    frame.fogStrength = std::max(config.farTerrainFog, 0.0f);
    frame.only = view.only;
    frame.share = view.share;

    if (!poll()) return;

    frame.tiles = (int) m_tiles.size();

    const float sceneX = view.perspective(0, 0);
    const float sceneY = view.perspective(1, 1);
    const float a = view.perspective(2, 2);
    const float b = view.perspective(2, 3);
    const float sceneNear = std::fabs(a) > 1e-6f ? b / (2.0f * a) : 1.0f;

    // The near plane reaches the camera while the full scene fades, since the far land is what replaces it, nearby too.
    const bool fading = view.only || view.share < 1.0f;
    const float nearPlane = fading ? sceneNear : std::max(sceneNear, view.drawDistance * kNearFraction);

    // The extent.
    const float xMax = AdtIndexToWorldCoordinate((float) m_minRow);
    const float xMin = AdtIndexToWorldCoordinate((float) (m_maxRow + 1));
    const float yMax = AdtIndexToWorldCoordinate((float) m_minCol);
    const float yMin = AdtIndexToWorldCoordinate((float) (m_maxCol + 1));
    const float dz = std::max(std::fabs(view.cameraPos.z - m_minHeight), std::fabs(view.cameraPos.z - m_maxHeight));
    float extent = 0.0f;
    for (float cornerX : {xMin, xMax}) {
        for (float cornerY : {yMin, yMax}) {
            const float dx = cornerX - view.cameraPos.x;
            const float dy = cornerY - view.cameraPos.y;
            extent = std::max(extent, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    extent = std::max(extent * 1.01f, nearPlane * 4.0f);
    frame.extent = extent;

    // The frustum, as the camera's culling builds its own.
    const float fovY = 2.0f * std::atan(1.0f / sceneY);
    const float aspect = sceneY / sceneX;
    // mathfu's Perspective reaches twice the far plane it is given, along the view axis.
    const mathfu::mat4 cullProjection = mathfu::mat4::Perspective(fovY, aspect, nearPlane, extent * 0.5f, 1.0f);
    const mathfu::mat4 cullMatrix = cullProjection * view.lookAt;
    const auto planes = MathHelper::getFrustumClipsFromMatrix(cullMatrix);

    // Tiles whose ADT has loaded, and the full terrain draws. While the full scene fades none is left out, since the far land has
    // to show under it, and it is the far land that shows through where the screen-door lets it.
    std::array<bool, 64 * 64> loaded;
    loaded.fill(false);
    for (const auto &adt : adtArray) {
        if (adt.adtObject->isLoaded() && view.share >= 1.0f) {
            loaded[adt.adtObject->getAdtY() * 64 + adt.adtObject->getAdtX()] = true;
        }
    }

    // How far a tile's box reaches along the view axis, which is where the culling frustum's far plane stands.
    auto reachesPastDrawDistance = [&](const CAaBox &box) {
        for (int corner = 0; corner < 8; corner++) {
            const mathfu::vec4 point(
                (corner & 1) ? box.max.x : box.min.x,
                (corner & 2) ? box.max.y : box.min.y,
                (corner & 4) ? box.max.z : box.min.z,
                1.0f);
            if (-(view.lookAt * point).z > view.drawDistance) return true;
        }
        return false;
    };

    struct Candidate {
        uint16_t index;
        float distance;
        uint8_t level;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(m_tiles.size());
    float nearestBox = std::numeric_limits<float>::max();
    for (size_t i = 0; i < m_tiles.size(); i++) {
        const Tile &tile = m_tiles[i];
        if (loaded[tile.row * 64 + tile.col] && !reachesPastDrawDistance(tile.aabb)) continue;

        const CAaBox &box = tile.aabb;
        bool outside = false;
        for (const auto &plane : planes) {
            const float distance =
                std::max(box.min.x * plane.x, box.max.x * plane.x) +
                std::max(box.min.y * plane.y, box.max.y * plane.y) +
                std::max(box.min.z * plane.z, box.max.z * plane.z) +
                plane.w;
            if (distance < 0.0f) {
                outside = true;
                break;
            }
        }
        if (outside) continue;

        const float bx = std::max(std::max(box.min.x - view.cameraPos.x, view.cameraPos.x - box.max.x), 0.0f);
        const float by = std::max(std::max(box.min.y - view.cameraPos.y, view.cameraPos.y - box.max.y), 0.0f);
        const float bz = std::max(std::max(box.min.z - view.cameraPos.z, view.cameraPos.z - box.max.z), 0.0f);
        const float boxDistance = std::sqrt(bx * bx + by * by + bz * bz);
        nearestBox = std::min(nearestBox, boxDistance);

        // The coarser grids from Config's distances, whose cells are 8.3, 16.7 and 33 yards.
        const uint8_t level = boxDistance >= config.farGridQuarterDistance ? 2 : boxDistance >= config.farGridHalfDistance ? 1 : 0;

        const float cx = (box.min.x + box.max.x) * 0.5f - view.cameraPos.x;
        const float cy = (box.min.y + box.max.y) * 0.5f - view.cameraPos.y;
        candidates.push_back({(uint16_t) i, cx * cx + cy * cy, level});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &l, const Candidate &r) { return l.distance < r.distance; });
    frame.drawn.reserve(candidates.size());
    frame.levels.reserve(candidates.size());
    for (const auto &candidate : candidates) {
        frame.drawn.push_back(candidate.index);
        frame.levels.push_back(candidate.level);
    }

    // The near plane of the depth, and with it the clip: the nearest the visible far land can be where the view is alone or fading.
    float depthNear = nearPlane;
    if (fading && !candidates.empty()) {
        const float cornerCosine = 1.0f / std::sqrt(1.0f + 1.0f / (sceneX * sceneX) + 1.0f / (sceneY * sceneY));
        depthNear = std::min(std::max(nearestBox * cornerCosine * kDepthNearMargin, nearPlane), extent * 0.25f);
    }

    // The projection for the draw, with the depth folded in.
    const float inverseRange = 1.0f / depthNear - 1.0f / extent;
    const float p22 = -1.0f / (depthNear * inverseRange);
    const float p23 = -1.0f / inverseRange;
    const mathfu::mat4 projection(
        sceneX, 0, 0, 0,
        0, -sceneY, 0, 0,
        0, 0, p22, -1,
        0, 0, p23, 0
    );
    // The view without its translation: the shader subtracts the camera from each vertex, so the product is of numbers the size
    // of the distance to the camera and not of the size of the map.
    mathfu::mat4 rotation = view.lookAt;
    rotation(0, 3) = 0.0f;
    rotation(1, 3) = 0.0f;
    rotation(2, 3) = 0.0f;
    frame.viewProjection = projection * rotation;
    frame.cameraPos = view.cameraPos;

    // The minimap textures: the ones in view first, then the rest of the map, a few a frame.
    int landed = 0;
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);

        int budget = kTexturesPerFrame;
        auto request = [&](size_t index) {
            if (budget <= 0 || m_textures[index] != nullptr || m_tiles[index].minimapFileDataId == 0 || (m_tiles[index].hasColor && !m_colorFailed[index])) return;
            m_textures[index] = m_api->cacheStorage->getTextureCache()->getFileId(m_tiles[index].minimapFileDataId);
            budget--;
        };
        for (uint16_t index : frame.drawn) request(index);
        for (size_t index = 0; index < m_tiles.size(); index++) request(index);

        for (const auto &texture : m_textures) {
            if (texture != nullptr && texture->getStatus() == FileStatus::FSLoaded) landed++;
        }
    }
    frame.textures = landed;

    if (!m_reachLogged) {
        m_reachLogged = true;
        // The culling frustum's far plane stands where the clip depth reaches its w, which mathfu's projection does at b / (1 + a),
        // twice the far distance it was given.
        const float cullingReach = std::fabs(1.0f + a) > 1e-6f ? b / (1.0f + a) : 0.0f;
        std::cerr << "far terrain: extent " << std::fixed << std::setprecision(0) << extent << " yd, near plane " << depthNear
                  << " yd, draw distance " << view.drawDistance << " yd, culling frustum reaches " << cullingReach
                  << " yd along the view axis" << std::endl;
    }
}

void FarTerrain::checkAdtCorners(const std::vector<ADTObjRenderRes> &adtArray) {
    if (m_cornersChecked || !m_ready) return;

    for (const auto &res : adtArray) {
        const auto &adt = res.adtObject;
        std::array<float, 17 * 17> corners;
        if (!adt->getCornerHeights(corners)) continue;

        WdlFile::Tile source;
        if (!m_wdl->getTile(adt->getAdtY(), adt->getAdtX(), source)) continue;

        const auto [lowest, highest] = std::minmax_element(corners.begin(), corners.end());
        // A flat tile agrees under any axes, so it proves nothing.
        if (*highest - *lowest < 10.0f) continue;

        float difference = 0.0f, transposed = 0.0f;
        for (int r = 0; r < 17; r++) {
            for (int c = 0; c < 17; c++) {
                difference = std::max(difference, std::fabs(corners[r * 17 + c] - (float) source.outer[r * 17 + c]));
                transposed = std::max(transposed, std::fabs(corners[r * 17 + c] - (float) source.outer[c * 17 + r]));
            }
        }

        std::ostringstream line;
        line << "far terrain: wdl corners of " << (m_mapName.empty() ? "map" + std::to_string(m_mapId) : m_mapName) << "_"
             << adt->getAdtX() << "_" << adt->getAdtY() << " within " << std::fixed << std::setprecision(3) << difference
             << " yd of the adt";
        std::cerr << line.str() << std::endl;
        if (difference > 1.0f) {
            std::cerr << "far terrain: the wdl axes are wrong, the transposed grid is within " << transposed << " yd" << std::endl;
        }
        m_cornersChecked = true;
        return;
    }
}
