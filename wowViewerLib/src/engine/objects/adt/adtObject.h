//
// Created by deamon on 10.07.17.
//

#ifndef WEBWOWVIEWERCPP_ADTOBJECT_H
#define WEBWOWVIEWERCPP_ADTOBJECT_H

class AdtObject;
class M2Object;

#include <array>
#include <vector>
#include <set>
#include <mutex>
#include <tbb/concurrent_unordered_set.h>

#include "../../persistance/header/adtFileHeader.h"

#include "../../persistance/adtFile.h"
#include "../../persistance/wdtFile.h"
#include "../m2/m2Object.h"
#include "../wmo/wmoObject.h"
#include "../worldObject/PlacedWmo.h"
#include "../iMapApi.h"
#include "../ViewsObjects.h"
#include "../liquid/LiquidInstance.h"
#include "../liquid/liquidMaterials/LiquidMaterialManager.h"

typedef std::function<bool(bool doCheck, bool doUpdate, animTime_t currentTime)> FreeStrategy;

class AdtObject;
enum class AdtObjectId : uintptr_t;
typedef EntityFactory<50, AdtObjectId, AdtObject> ADTObjectEntityFactory;
extern std::shared_ptr<ADTObjectEntityFactory> adtObjectFactory;
class AdtObject : public ObjectWithId<AdtObjectId> {
public:
    AdtObject(const HApiContainer &api, std::string &adtFileTemplate, const std::string &mapname, int adt_x, int adt_y, bool useWeightedBlend, const HWdtFile &wdtfile);
    AdtObject(const HApiContainer &api, int adt_x, int adt_y, WdtFile::MapFileDataIDs &fileDataIDs, bool useWeightedBlend, const HWdtFile &wdtfile);
    ~AdtObject() {
//        std::cout << "~AdtObject called" << std::endl;
    };

    void setFreeStrategy(FreeStrategy &freeStrat) {
        m_freeStrategy = freeStrat;
        m_freeStrategy(false, true, m_mapApi->getCurrentSceneTime());
    }

    void setMapApi(IMapApi *api) {
        m_mapApi = api;
    }

    FileStatus getLoadedStatus();
    // True once every file of the tile has loaded.
    bool filesReady();

    void collectMeshes(ADTObjRenderRes &adtRes, COpaqueMeshCollector &opaqueMeshCollector, framebased::vector<HGSortableMesh> &transparentMeshes);
    void collectMeshesLod(std::vector<HGMesh> &renderedThisFrame);

    void update(animTime_t deltaTime);
    void uploadGeneratorBuffers(const HFrameDependantData &frameDependantData);
    // Runs the tile's next load stage and returns true when it ran one, or all the rest with allStages. A tile needs five.
    bool doPostLoad(const HMapSceneBufferCreate &sceneRenderer, const std::unique_ptr<LiquidMaterialManager> &liquidMaterialManager, bool allStages = false);
    // Ranks the tile's own files in the loader queue at distanceSq, in yards squared (fileRank.h).
    void rankFiles(float distanceSq);

    int getAreaId(int mcnk_x, int mcnk_y);
    void getHeight(const mathfu::vec4 &camera, float &height);
    // The tile's 17 x 17 chunk-corner heights, absolute: the first vertex of each chunk, and the last row and column of the last
    // chunks. The first index runs along the tile's rows (world x falling), the second along its columns (world y falling), as the
    // WDL's outer grid does. False until the ADT file has loaded, or where a chunk is missing.
    bool getCornerHeights(std::array<float, 17 * 17> &heights);

    // True once the tile's files have loaded and its meshes are made.
    bool isLoaded() const {return m_loaded;}

    int getAdtX() {return adt_x;}
    int getAdtY() {return adt_y;}

    CAaBox calcAABB();

    // The box the map culls the tile by: its terrain and water chunks and every object it owns. False until the tile has
    // finished loading, so a tile still loading is never culled by it.
    bool getCullBox(CAaBox &box);
    // Widens the tile's box, for an object added after the tile finished loading.
    void widenCullBox(const CAaBox &box);
    // Counts what the tile places that a bake of the square from min to max (world x and y) draws: its models of 30 yards or more, its
    // WMOs and the groups of those the square reaches. `pending` is how many of `total` have not loaded, and `pendingBy` splits
    // them into models, WMOs and WMO groups. A model counts by the box the map culls it by, which is its placement's until it has loaded.
    void countBakeResidency(float minX, float maxX, float minY, float maxY, int &total, int &pending, int (&pendingBy)[3]);
    bool getWaterColorFromDB(mathfu::vec4 cameraPos, mathfu::vec3 &closeRiverColor);

    bool checkFrustumCulling(
        ADTObjRenderRes &adtFrustRes,
        const mathfu::vec4 &cameraPos,
        const MathHelper::FrustumCullingData &frustumData,

        M2ObjectListContainer&m2ObjectsCandidates,
        WMOListContainer &wmoCandidates);

    bool
    checkReferences(ADTObjRenderRes &adtFrustRes,
                    const mathfu::vec4 &cameraPos,
                    const MathHelper::FrustumCullingData &frustumData,
                    int lodLevel,
                    M2ObjectListContainer &m2ObjectsCandidates,
                    WMOListContainer &wmoCandidates,
                    int x, int y, int x_len, int y_len);

    FreeStrategy &getFreeStrategy() {
        return m_freeStrategy;
    }
private:
    FreeStrategy m_freeStrategy = [](bool doCheck, bool doUpdate, animTime_t currentTime) -> bool {return false;};

    animTime_t m_lastTimeOfUpdate = 0;
    animTime_t m_lastDeltaTime = 0;

    struct LodCommand {
        int index;
        int length;
    };

    void loadingFinished(const HMapSceneBufferCreate &sceneRenderer, const std::unique_ptr<LiquidMaterialManager> &liquidMaterialManager, bool allStages);

    void createVBO(const HMapSceneBufferCreate &sceneRenderer);
    void createIBOAndBinding(const HMapSceneBufferCreate &sceneRenderer);

    void createMeshes(const HMapSceneBufferCreate &sceneRenderer);
    void loadAlphaTextures();

    HApiContainer m_api;
    IMapApi *m_mapApi;
    HWdtFile m_wdtFile= nullptr;

    HAdtFile m_adtFile = nullptr;
    HAdtFile m_adtFileTex = nullptr;
    HAdtFile m_adtFileObj = nullptr;
    HAdtFile m_adtFileObjLod = nullptr;
    HAdtFile m_adtFileLod = nullptr;

    int alphaTexturesLoaded = 0;
    bool m_loaded = false;
    int m_stage = 0;

    int mostDetailedLod = 0; // 0 = most detailed LOD, 5 = least detailed lod
    int leastDetiledLod = 0;

    robin_hood::unordered_flat_map<int, HGSamplableTexture> m_requestedTextures;
    robin_hood::unordered_flat_map<int, HGSamplableTexture> m_requestedTexturesHeight;
    robin_hood::unordered_flat_map<int, HGSamplableTexture> m_requestedTexturesSpec;

    std::vector<LodCommand> lodCommands;

    HGVertexBuffer combinedVbo ;
    HGIndexBuffer stripIBO ;
    HGVertexBufferBindings adtVertexBindings;
    bool m_holesIgnored = false;
    bool m_adtColorsIgnored = false;

    HGVertexBuffer heightVboLod;
    HGIndexBuffer stripVBOLod;

    HGVertexBufferBindings lodVertexBindings;

private:
    HGSamplableTexture alphaTexture;
    HGSamplableTexture alphaTexture2;
    HBlpTexture lodDiffuseTexture  = nullptr;
    HBlpTexture lodNormalTexture  = nullptr;


    std::array<HGMesh, 16*16> adtMeshes = {};
    std::array<std::shared_ptr<IADTMaterial>, 16*16> adtMaterials = {};
    //16x16, then layer
    std::array<std::vector<std::shared_ptr<LiquidInstance>>, 16*16> m_liquidInstancesPerChunk = {};
    std::vector<std::shared_ptr<LiquidInstance>> m_liquidInstances = {};
    std::vector<HGMesh> adtLodMeshes;

    // The cull box, written when the tile finishes loading on the render thread and read by the culling thread.
    std::mutex m_cullBoxMutex;
    bool m_hasCullBox = false;
    mathfu::vec3 m_cullBoxMin = mathfu::vec3(0, 0, 0);
    mathfu::vec3 m_cullBoxMax = mathfu::vec3(0, 0, 0);

    std::vector<CAaBox> tileAabb;
    std::vector<CAaBox> waterTileAabb;
    //Per-chunk flags for chunks that actually have liquid instances (set in loadWater).
    //waterTileAabb entries are only valid for flagged chunks.
    std::array<uint8_t, 16*16> m_chunkHasWater = {};
    bool m_hasAnyWater = false;
    std::vector<int> globIndexX;
    std::vector<int> globIndexY;

    int adt_x;
    int adt_y;

    bool m_useWeightedBlend;

    std::string m_adtFileTemplate;


    struct lodLevels {
        std::vector<std::shared_ptr<M2Object>> m2Objects;
        std::vector<std::shared_ptr<WmoObject>> wmoObjects;

        std::vector<M2ObjId> m2ObjectIds;
        std::vector<WMOObjId> wmoObjectIds;
    };
    std::array<lodLevels, 2> objectLods;

    // The creatures the tile's CreatureSpawn rows place, and the game objects its GameObjectSpawn rows do. The tile holds the only
    // strong refs, so they go with it.
    std::vector<std::shared_ptr<PlacedCreature>> m_creatures;
    // The game objects of the tile whose model is a WMO. They are candidates of every frame the tile is collected in.
    std::vector<PlacedWmo> m_gameObjectWmos;

    HGSamplableTexture getAdtTexture(int textureId);
    HGSamplableTexture getAdtHeightTexture(int textureId);
    HGSamplableTexture getAdtSpecularTexture(int textureId);

    void calcBoundingBoxes();
    void calcCullBox();
    void loadM2s();
    void loadCreatures();
    void loadWmos();
    // Asks for the files of every model, building and spawn the tile placed, under the caller's rank scope.
    void requestObjects();
    void loadWater(const HMapSceneBufferCreate &sceneRenderer, const std::unique_ptr<LiquidMaterialManager> &liquidMaterialManager);

    bool checkNonLodChunkCulling(ADTObjRenderRes &adtFrustRes,
                                 const mathfu::vec4 &cameraPos,
                                 const MathHelper::FrustumCullingData &frustumData,
                                 int x, int y, int x_len, int y_len);

    bool
    iterateQuadTree(ADTObjRenderRes &adtFrustRes, const mathfu::vec4 &camera, const mathfu::vec3 &pos, float x_offset, float y_offset, float cell_len,
                    int currentLod, int lastFoundLod,
                    const PointerChecker<MLND> &quadTree,
                    int quadTreeInd,
                    const MathHelper::FrustumCullingData &frustumData,
                    M2ObjectListContainer &m2ObjectsCandidates,
                    WMOListContainer &wmoCandidates);

    void fillTextureForMCNK(HGDevice &device, int i, bool noLayers, ADTMaterialTemplate &adtMaterialTemplate);


    void
    checkWmoM2ByRef(int lodLevel, M2ObjectListContainer &m2ObjectsCandidates, WMOListContainer &wmoCandidates, int x,
                    int y,
                    int x_len, int y_len);

    void
    checkWmoGlobally(int lodLevel, M2ObjectListContainer &m2ObjectsCandidates, WMOListContainer &wmoCandidates, int x,
                    int y,
                    int x_len, int y_len);
};


#endif //WEBWOWVIEWERCPP_ADTOBJECT_H
