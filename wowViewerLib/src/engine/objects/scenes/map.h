//
// Created by Deamon on 7/16/2017.
//

#ifndef WEBWOWVIEWERCPP_MAP_H
#define WEBWOWVIEWERCPP_MAP_H


#include <unordered_set>
#include <map>
#include <set>
#include <atomic>
#include <chrono>
#include "../adt/adtObject.h"
#include "../m2/m2Object.h"
#include "../wmo/wmoObject.h"
#include "../iMapApi.h"
#include "../iScene.h"
#include "../objectCache.h"
#include "../wdl/wdlObject.h"
#include "tbb/tbb.h"
#include "../../algorithms/FrameCounter.h"
#include "../../../renderer/frame/FrameInputParams.h"
#include "../../../renderer/mapScene/MapScenePlan.h"
#include "../../../renderer/mapScene/MapSceneParams.h"
#include "../wdt/wdtLightsObject.h"
#include "dayNightDataHolder/DayNightLightHolder.h"
#include "../liquid/liquidMaterials/LiquidMaterialManager.h"
#include "../worldObject/WorldObjectManager.h"
#include "../worldObject/PlacedWmo.h"

enum class SceneMode {
   smMap,
   smM2,
   smWMO
};

class Map : public IScene, public IMapApi {
private:
    void initMapTiles() {
        for (auto &x : mapTiles) {
            for (auto &y : x) {
                y = nullptr;
            }
        }
    }
protected:
    HApiContainer m_api = nullptr;
    std::unique_ptr<LiquidMaterialManager> m_liquidMaterialManager;

    std::array<std::array<std::shared_ptr<AdtObject>, 64>, 64> mapTiles={};
    std::vector<std::array<uint8_t, 2>> m_mandatoryADT;
    std::string mapName;

    std::shared_ptr<IBufferChunk<sceneWideBlockVSPS>> m_sceneWideBlockVSPSChunk;

    SceneMode m_sceneMode = SceneMode::smMap;

    float m_currentTime = 0;
    bool m_lockedMap = false;

    int m_mapId = -1;
    HWdtFile m_wdtfile = nullptr;
    std::shared_ptr<WmoObject> wmoMap = nullptr;

    bool useWeightedBlend = false;
    bool has0x200000Flag = false;

    std::shared_ptr<WdlObject> m_wdlObject = nullptr;
    std::shared_ptr<WdtLightsObject> m_wdtLightObject = nullptr;

    // The land beyond the draw distance, made once the WDL and the WDT have loaded and kept while the map is open, once
    // Config's farTerrain has been on.
    std::shared_ptr<FarTerrain> m_farTerrain = nullptr;
    // The frame's view is the far terrain alone: Config's farTerrainOnly, read in makeFramePlan ahead of the tile requests.
    bool m_farTerrainOnly = false;
    void createFarTerrain();
    std::atomic<bool> m_bakeOn{false};
    std::atomic<float> m_bakeX{0}, m_bakeY{0}, m_bakeHalf{0};
    MapRenderPlan::BakeResidency bakeResidency(float x, float y, float half);

    HGMesh skyMesh = nullptr;
    std::shared_ptr<ISkyMeshMaterial> skyMeshMat = nullptr;

    HGMesh skyMesh0x4Sky = nullptr;
    std::shared_ptr<ISkyMeshMaterial> skyMeshMat0x4 = nullptr;

    // Planets (sun/moon discs) + stars
    std::array<HGSamplableTexture, 3> m_planetTextures = {nullptr, nullptr, nullptr}; // sun, moon1, moon2
    std::array<HGMesh, 3> m_planetMeshes = {nullptr, nullptr, nullptr};
    std::array<std::shared_ptr<IPlanetMaterial>, 3> m_planetMats = {nullptr, nullptr, nullptr};
    std::shared_ptr<M2Object> m_starsModel = nullptr;

    //Map mode
    std::unordered_map<int, std::weak_ptr<M2Object>> m_m2MapObjects = {};
    std::unordered_map<int, std::weak_ptr<WmoObject>> m_wmoMapObjects = {};

    //M2 mode
    std::shared_ptr<M2Object> m_m2Object = nullptr;
    std::string m_m2Model;

    //Wmo mode
    std::shared_ptr<WmoObject> m_wmoObject = nullptr;
    std::string m_wmoModel;
    ///end

    bool m_suppressDrawingSky = false;

    std::shared_ptr<M2Object> getM2Object(std::string fileName, const SMDoodadDef &doodadDef) override ;
    std::shared_ptr<M2Object> getM2Object(int fileDataId, const SMDoodadDef &doodadDef) override ;
    std::shared_ptr<WmoObject> getWmoObject(std::string fileName, const SMMapObjDef &mapObjDef, const PointerChecker<MWDR> &m_MWDR, const PointerChecker<uint16_t> &m_MWDS) override ;
    std::shared_ptr<WmoObject> getWmoObject(int fileDataId, const SMMapObjDef &mapObjDef, const PointerChecker<MWDR> &m_MWDR, const PointerChecker<uint16_t> &m_MWDS) override ;
    std::shared_ptr<WmoObject> getWmoObject(std::string fileName, const SMMapObjDefObj1 &mapObjDef, const PointerChecker<MWDR> &m_MWDR, const PointerChecker<uint16_t> &m_MWDS) override ;
    std::shared_ptr<WmoObject> getWmoObject(int fileDataId, const SMMapObjDefObj1 &mapObjDef, const PointerChecker<MWDR> &m_MWDR, const PointerChecker<uint16_t> &m_MWDS) override ;


    virtual void getPotentialEntities(
        const MathHelper::FrustumCullingData &frustumData,
        const mathfu::vec4 &cameraPos,
        const HMapRenderPlan &mapRenderPlan,
        M2ObjectListContainer &potentialM2,
        WMOListContainer &potentialWmo);

    virtual void getCandidatesEntities(const MathHelper::FrustumCullingData &frustumData,
                                       const mathfu::vec4 &cameraPos,
                                       const HMapRenderPlan &mapRenderPlan,
                                       M2ObjectListContainer &m2ObjectsCandidates,
                                       WMOListContainer &wmoCandidates);

    void checkADTCulling(int i, int j,
                         const MathHelper::FrustumCullingData &frustumData,
                         const mathfu::vec4 &cameraPos,
                         const HMapRenderPlan &mapRenderPlan,
                         M2ObjectListContainer &m2ObjectsCandidates,
                         WMOListContainer &wmoCandidates);

    // Fills mapTiles[i][j] and starts its file requests. Leaves it empty when the map has no tile there.
    void createAdtTile(int i, int j);
    void preloadTilesAhead(const mathfu::vec3 &point, const HMapRenderPlan &mapRenderPlan);

    void getPossibleHeight(const mathfu::vec4 &cameraPos, float &height);

    virtual void updateLightAndSkyboxData(const HMapRenderPlan &mapRenderPlan, MathHelper::FrustumCullingData &frustumData,
                                          StateForConditions &stateForConditions, const AreaRecord &areaRecord);

    FreeStrategy adtFreeLambda;
    FreeStrategy zeroStateLambda;

    HWorldObjectManager m_worldObjectManager = nullptr;

    // The creatures and game objects of a map with no tiles, read once its WDT says so. The map holds the only strong refs, so they go
    // with it.
    std::vector<std::shared_ptr<PlacedCreature>> m_mapCreatures;
    std::vector<PlacedWmo> m_mapGameObjectWmos;
    bool m_mapCreaturesLoaded = false;
    void collectMapCreatures(M2ObjectListContainer &m2ObjectsCandidates, WMOListContainer &wmoCandidates);

protected:
    explicit Map(const HApiContainer &m_api) : m_dayNightLightHolder(m_api, -1) {
    }
    DayNightLightHolder m_dayNightLightHolder;
public:
    explicit Map(HApiContainer api, int mapId, const std::string &mapName);

    explicit Map(HApiContainer api, int mapId, int wdtFileDataId);;

    explicit Map(HApiContainer api, std::string adtFileName, int i, int j, std::string mapName);;
    void initialize(HApiContainer api, int mapId);

    ~Map() override {
//        std::cout << "Map destroyed " << std::endl;
	};
    animTime_t getCurrentSceneTime() override;
    mathfu::vec3 getGlobalOffset() override;

    void makeFramePlan(const FrameInputParams<MapSceneParams> &frameInputParams, const HMapRenderPlan &mapRenderPlan);

    void setMandatoryADTs(std::vector<std::array<uint8_t, 2>> &mandatoryADTs) {
        m_mandatoryADT = mandatoryADTs;
    }
    void getAdtAreaId(const mathfu::vec4 &cameraPos, int &areaId, int &parentAreaId);

    void setWorldObjectManager(HWorldObjectManager worldObjectManager) {
        m_worldObjectManager = worldObjectManager;
    }
    int getMapId() override { return m_mapId; }
    HWorldObjectManager getWorldObjectManager() const {
        return m_worldObjectManager;
    }

    // Sky scene ids of the map's WDL grouped by the player condition that gates them
    // (SkySceneXPlayerCondition db2). Empty when the map has no WDL. Built once at WDL
    // load and static for the WDL's lifetime.
    const std::map<int, std::set<int>> &getSkyScenesByPlayerCondition() const {
        static const std::map<int, std::set<int>> empty = {};
        return m_wdlObject ? m_wdlObject->getSkyScenesByPlayerCondition() : empty;
    }

    // A height bake's view (src/flyover/HeightBake.cpp): while on, every plan counts what is still loading in the square of `half` yards
    // around (x, y) into MapRenderPlan::bake. Set from the main thread, read by the culling thread.
    void setBakeView(bool on, float x, float y, float half);
    // The WDL's ground height at a world position, where the map has a WDL tile there; makes the far terrain's holder if it is not made.
    // False until the WDL has loaded. Main thread.
    bool wdlGround(float x, float y, float &height);
    // 🤖 The FileDataID of a continent tile's root ADT from the WDT, 0 where the tile has none or the WDT has not loaded. Main thread.
    uint32_t rootAdtFileDataId(int row, int col) const;
    // 🤖 The tile light's questions to the map (DayNightLightHolder::setTileLightSource): whether the WDT and the WDL have answered, and
    // whether the WDT has an ADT at a tile. Both run on the culling thread.
    bool tileLightSourceReady() const;
    bool adtTileExists(int row, int col) const;
    void doPostLoad(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan);

    // A map is about to open: the Map made next finishes its tiles and geometry without the per-frame budgets until the tiles
    // around the arrival point are in, or FAST_LOAD_CEILING has passed. Called from SceneWindow::unload, ahead of every open.
    static void requestFastLoad();
    // The time a frame is paced to, from the main loop each frame; 0 when it is not paced. Sizes the load budgets.
    static void setFramePeriod(std::chrono::microseconds period);
    // What a frame spends on tile stages, and again on finishing geometry (map_load_max_contants.h).
    static std::chrono::microseconds loadBudget();

    void update(const HMapRenderPlan &renderPlan);
    void updateBuffers(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan);
private:
    // The open this map belongs to, taken when it is made; a map from an earlier open never takes part in the window.
    const uint64_t m_fastLoadGeneration = currentFastLoadGeneration();
    // A map with no tiles, one WMO, has nothing to wait on until its queue has held work once.
    bool m_fastLoadSawWork = false;
    static uint64_t currentFastLoadGeneration();
    // Whether this map's window is open; closes it, logging, once the ceiling has passed.
    bool fastLoadOpen();
    bool wdtLoaded() const;
    // Whether the frame is settled: see the fast-load window in map.cpp.
    bool loadSettled(bool tilesAsked, bool tilesLoaded, size_t queued);

    // Returns how many items were in the queue when it was cut, the ones it finished included.
    size_t finishLoadedGeometry(const HMapSceneBufferCreate &sceneRenderer, const HMapRenderPlan &renderPlan, bool fastLoad);
    void checkExterior(mathfu::vec4 &cameraPos,
                       const MathHelper::FrustumCullingData &frustumData,
                       const HMapRenderPlan &mapRenderPlan);

//    HDrawStage doGaussBlur(const HDrawStage &parentDrawStage, std::vector<HGUniformBufferChunk> &uniformBufferChunks) const;

    void createAdtFreeLamdas();
};
typedef std::shared_ptr<Map> HMapScene;

#endif //WEBWOWVIEWERCPP_MAP_H
