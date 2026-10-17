#ifndef WOWVIEWERLIB_TILELIGHTGRID_H
#define WOWVIEWERLIB_TILELIGHTGRID_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <mathfu/glsl_mappings.h>
#include "LightParamCalculate.h"

/*
 * The light of every map tile for Config::tileLight: the exterior and water colours a camera standing at the tile's centre would
 * get, as the buffer the shaders read (commonTileLight.slang). The blends of zone lights at each centre are worked out once per map;
 * DayNightLightHolder colours them whenever the time changes.
 */
class TileLightGrid {
public:
    static constexpr int kSide = 64;
    static constexpr int kTiles = kSide * kSide;
    // Per tile: ambient, horizon ambient, ground ambient, direct, specular, close river, far river, close ocean, far ocean.
    static constexpr int kVec4sPerTile = 9;
    static constexpr int kVec4s = kTiles * kVec4sPerTile;

    // What the grid asks of the map.
    struct Source {
        // True once the files the answers below read have loaded.
        std::function<bool()> ready;
        // True for a row and column where the map has a tile.
        std::function<bool(int row, int col)> exists;
        // The WDL's ground height at a world position, false where it has none.
        std::function<bool(float x, float y, float &height)> ground;
    };

    // Works out the blends of up to maxTiles tiles that exist, from the point the last call stopped at.
    void advanceBlends(const std::shared_ptr<IClientDatabase> &databaseHandler, int mapId,
                       const std::vector<mapInnerZoneLightRecord> &zoneLights, const Source &source, int maxTiles);

    bool blendsReady() const { return m_next >= kTiles; }
    // The light params every tile mixes, sorted and without repeats. Valid once blendsReady.
    const std::vector<int> &paramIds() const { return m_paramIds; }
    // The params a tile mixes and by how much, in the order the camera's light mixes them. Empty for a tile the map lacks.
    const std::vector<IdAndBlend> &blendsOf(int tile) const { return m_blends[tile]; }

    // Writes one tile of the colouring in progress.
    void setTile(int tile, bool valid, const ExteriorColors &exterior, const LiquidColors &liquid);
    // Makes the colouring in progress the one the renderer reads, as the light of the given time of day.
    void publish(int time);

    // The time of day the published colouring is of, or -1 before the first.
    int colouredAt() const { return m_colouredAt; }
    // Counts the colourings published. 0 before the first.
    uint32_t version() const { return m_version; }
    const std::shared_ptr<const std::vector<mathfu::vec4_packed>> &published() const { return m_published; }

private:
    std::array<std::vector<IdAndBlend>, kTiles> m_blends;
    std::vector<int> m_paramIds;
    int m_next = 0;

    std::vector<mathfu::vec4_packed> m_work = std::vector<mathfu::vec4_packed>(kVec4s);
    std::shared_ptr<const std::vector<mathfu::vec4_packed>> m_published;
    int m_colouredAt = -1;
    uint32_t m_version = 0;
};

#endif //WOWVIEWERLIB_TILELIGHTGRID_H
