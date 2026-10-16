#ifndef WOWVIEWERLIB_FARLANDMARKS_H
#define WOWVIEWERLIB_FARLANDMARKS_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../ApiContainer.h"
#include "../../persistance/wdtFile.h"
#include "../../texture/BlpTexture.h"
#include "farTerrain.h"

class AdtFile;
class M2Geom;
class SkinGeom;
class WmoMainGeom;
class WmoGroupGeom;

/*
 * The landmarks of a map: every placement of the M2 models and WMOs its landmarks.ids names (FarHeights), found in the object file of every
 * tile and in the map's GameObjectSpawn rows, flattened into world-space geometry with one draw for each texture batch. The far pass draws
 * them at any distance, unfogged.
 * The map's FarTerrain makes one when it opens a map whose file names any.
 */
class FarLandmarks {
public:
    struct Draw {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        // Into Geometry::textures.
        uint16_t texture = 0;
        // The batch cuts its texture's transparent texels away (an alpha-keyed material).
        bool alphaTest = false;
    };

    struct Geometry {
        std::vector<FarTerrainVertex> vertices;
        std::vector<uint16_t> indices;
        std::vector<Draw> draws;
        std::vector<HBlpTexture> textures;
    };

    // An entry naming a group of a WMO, or a batch of a model's first skin, draws that alone, a batch solid whatever its material; one
    // naming a WMO whole draws its exterior groups.
    FarLandmarks(const HApiContainer &api, const HWdtFile &wdt, int mapId, const std::vector<IdEntry> &ids);

    // Steps the load: the tiles' object files, then the models and WMOs, then the geometry. True once the geometry is built.
    bool poll();

    // The geometry, once poll has returned true; empty when no placement matched.
    const Geometry &geometry() const { return m_geometry; }

private:
    struct Placement {
        bool wmo = false;
        int fileId = 0;
        mathfu::mat4 matrix;
    };

    void scan(const AdtFile &file);
    void scanSpawns();
    void request();
    bool loaded();
    void build();
    uint16_t textureSlot(const HBlpTexture &texture);

    HApiContainer m_api;
    HWdtFile m_wdt;
    int m_mapId;
    // The models and WMOs named whole, and the WMOs named by some groups alone with those groups.
    std::unordered_set<int> m_whole;
    std::unordered_map<int, std::unordered_set<int>> m_onlyGroups;

    enum class Phase { Scan, Load, Done };
    Phase m_phase = Phase::Scan;
    // The next tile of the MAID whose object file the scan asks for.
    int m_nextTile = 0;
    std::vector<std::shared_ptr<AdtFile>> m_objectFiles;
    std::unordered_set<uint32_t> m_seen;
    std::vector<Placement> m_placements;

    std::unordered_map<int, std::shared_ptr<M2Geom>> m_m2s;
    std::unordered_map<int, std::shared_ptr<SkinGeom>> m_skins;
    std::unordered_map<int, std::shared_ptr<WmoMainGeom>> m_wmos;
    std::unordered_map<int, std::vector<std::shared_ptr<WmoGroupGeom>>> m_groups;

    Geometry m_geometry;
    std::unordered_map<BlpTexture *, uint16_t> m_textureSlots;
};

#endif //WOWVIEWERLIB_FARLANDMARKS_H
