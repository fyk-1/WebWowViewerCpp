//
// Created by deamon on 03.07.17.
//

#ifndef WEBWOWVIEWERCPP_WMOOBJECT_H
#define WEBWOWVIEWERCPP_WMOOBJECT_H
#include <bitset>
#include <mutex>

struct WmoGroupResult;
class WmoGroupObject;
#include <string>
#include <unordered_set>
#include <oneapi/tbb/concurrent_unordered_set.h>
#include <unordered_map>
#include <memory>
#include "../../geometry/wmoMainGeom.h"

#include "../../persistance/header/adtFileHeader.h"
#include "mathfu/glsl_mappings.h"
#include "wmoGroupObject.h"
#include "../m2/m2Object.h"
#include "../iWmoApi.h"
#include "../../persistance/header/wmoFileHeader.h"
#include "../ViewsObjects.h"
#include "../../../include/database/dbStructs.h"
#include "../SceneObjectWithID.h"
#include "../lights/CEngineLight.h"
#include "../worldObject/PlacedCreature.h"

enum class WMOObjId : uintptr_t;
constexpr WMOObjId emptyWMO = static_cast<const WMOObjId>(0xFFFFFFF);

class WmoObject : public IWmoApi, public ObjectWithId<WMOObjId>{

public:
    WmoObject(const HApiContainer &api/*, int id*/) : /*SceneObjectWithId(id),*/ m_api(api) {
        //DoodadSet 0 is always active
        m_activeDoodadSets.set(0);
    }

	~WmoObject();
private:
    HApiContainer m_api;

    HWmoMainGeom mainGeom = nullptr;

    bool m_loading = false;
    bool m_loaded = false;
    CAaBox m_bbox;

    //Per-group world-space bounding boxes, owned here (contiguous) so batched SIMD culling can
    //run over them. [i] corresponds to groupObjects[i]. m_groupWorldBorders includes loaded
    //doodad M2s; m_groupVolumeWorldBorders is group geometry only.
    std::vector<CAaBox> m_groupWorldBorders;
    std::vector<CAaBox> m_groupVolumeWorldBorders;
    //Scratch buffers for batch cull results, reused across calls to avoid per-call allocation
    std::vector<uint32_t> m_groupWorldVisScratch;
    std::vector<uint32_t> m_groupVolumeVisScratch;

    //Precomputed at doPostLoad from mainGeom->groups flags. Used by Map::checkExterior to decide
    //whether a frustum-culled WMO must still be collected for its unconditional side effects.
    bool m_hasAlwaysDrawGroups = false;
    bool m_hasAntiportalGroups = false;

    int m_nameSet;
    ActiveDoodadSets m_activeDoodadSets;

    std::vector<PortalInfo_t> geometryPerPortal;

    bool m_placementMatChanged = false;
    mathfu::mat4 m_placementMatrix;
    mathfu::mat4 m_placementInvertMatrix;

    std::string m_modelName;
    bool useFileId = false;
    int m_modelFileId;

    std::vector<std::shared_ptr<WmoGroupObject>> groupObjects = std::vector<std::shared_ptr<WmoGroupObject>>(0);
    std::vector<std::shared_ptr<WmoGroupObject>> groupObjectsLod1 = std::vector<std::shared_ptr<WmoGroupObject>>(0);
    std::vector<std::shared_ptr<WmoGroupObject>> groupObjectsLod2 = std::vector<std::shared_ptr<WmoGroupObject>>(0);
    std::vector<BlpTexture> blpTextures;

    std::vector<std::shared_ptr<CEngineLight>> m_newLights;

    std::vector<bool> drawGroupWMO;
    std::vector<int> lodGroupLevelWMO;
    robin_hood::unordered_flat_map<int, std::weak_ptr<M2Object>> m_doodadsUnorderedMap;

    std::shared_ptr<M2Object> skyBox = nullptr;

    robin_hood::unordered_flat_map<int, HGSamplableTexture> diffuseTextures;
    robin_hood::unordered_flat_map<int, HGSamplableTexture> specularTextures;

    std::shared_ptr<IWmoModelData> m_wmoModelChunk;

    bool m_interiorAmbientsChanged = false;
    std::vector<WMO::InteriorBlockData> m_groupInteriorData;

    std::vector<std::shared_ptr<IWMOMaterial>> m_materialCache;

    HGMesh transformedAntiPortals;

    //Creatures a tile or the map offered, waiting for the groups they may stand in to load
    struct PendingCreature {
        std::weak_ptr<PlacedCreature> creature;
        mathfu::vec3 position;
        //Groups holding the position whose geometry is not loaded yet, as of the last time the creature was looked at
        std::vector<int> unloadedGroups;
    };
    std::mutex m_creatureMutex;
    std::vector<PendingCreature> m_pendingCreatures;
    std::atomic<bool> m_creaturesToHome{false};
    void homeCreatures();
    void declineCreatures();
    void applyInteriorLightingToCreature(M2Object &creature, int groupIndex, const mathfu::vec3 &position);
    int findNearestDoodadDef(int groupIndex, const mathfu::vec3 &position);
    bool isDoodadInActiveSets(int index);
    float getDoodadMddiVal(int index);

    //Ambient, GroundAmbient, HorizontalAmbient
    std::array<mathfu::vec3, 3> m_ambientColors;

    void createPlacementMatrix(const SMMapObjDef &mapObjDef);
    void createPlacementMatrix(const SMMapObjDefObj1 &mapObjDef);
    void createBB(CAaBox bbox);
    //Pushes m_bbox into the CAaBox component storage of wmoFactory (for batched frustum culling).
    //Must be called after every m_bbox mutation.
    void syncBBoxComponent();
    void postWmoGroupObjectLoad(int groupId, int lod) override;
    void fillLodGroup(mathfu::vec3 &cameraLocal);
    friend void attenuateTransVerts(HWmoMainGeom &mainGeom, WmoGroupGeom& wmoGroupGeom);
public:
    std::shared_ptr<M2Object> getDoodad(int index, int fromGroupIndex) override ;
    void applyLightingParamsToDoodad(const SMODoodadDef *doodadDef, M2Object* doodad, float mddiVal, int fromGroupIndex);
    void applyColorFromMOLT(
        const SMODoodadDef *doodadDef,
        M2Object *doodad,
        std::array<mathfu::vec3, 3> &interiorAmbients,
        mathfu::vec3 &color,
        bool &hasDoodad0x4Flag,
        int fromGroupIndex);

    HGSamplableTexture getTexture(int materialId, bool isSpec) override;
    void setLoadingParam(const SMMapObjDef &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS);
    void setLoadingParam(const SMMapObjDefObj1 &mapObjDef, const PointerChecker<MWDR> &MWDR, const PointerChecker<uint16_t> &MWDS);

    void setLoadingParam(mathfu::vec3 pos, mathfu::vec3 scaleVec, mathfu::mat4 *rotationMatrix,
                                const mathfu::vec4 &localAABBMin, const mathfu::vec4 &localAABBMax);

    std::string getModelFileName();
    void setModelFileName(std::string modelName);
    int getModelFileId();
    void setModelFileId(int fileId);

    void startLoading();
    bool isLoaded() override { return m_loaded;}
    bool isFailedToLoadGeom() { return mainGeom != nullptr && mainGeom->getStatus() == FileStatus::FSRejected; }
    bool hasPortals();
    int getNameSet() {
        return m_nameSet;
    }
    int getWmoId() {
        if (m_loaded) {
            return mainGeom->header->wmoID;
        }
        return 0;
    }
    int getWmoGroupId (int groupNum);

    std::function<void (WmoGroupGeom& wmoGroupGeom)> getAttenFunction() override;
    SMOHeader *getWmoHeader() override;
    ActiveDoodadSets getActiveDoodadSet() override {
        return m_activeDoodadSets;
    }
    std::array<mathfu::vec3, 3> getAmbientColors() override;

    PointerChecker<SMOMaterial> &getMaterials() override;
    std::shared_ptr<IWMOMaterial> getMaterialInstance(int index, const HMapSceneBufferCreate &sceneRenderer) override;

    PointerChecker<SMOLight> &getLightArray() override;
    std::vector<PortalInfo_t> &getPortalInfos() override {
        return geometryPerPortal;
    };
    std::shared_ptr<IBufferChunk<WMO::modelWideBlockVS>> getPlacementBuffer() override {
        return m_wmoModelChunk->m_placementMatrix;
    }
    uint32_t getPickObjectId() override {
        return static_cast<uint32_t>(this->ObjectWithId<WMOObjId>::getObjectId());
    }

    std::shared_ptr<M2Object> getSkyBoxForGroup (int groupNum);;
    void collectMeshes(std::vector<HGMesh> &renderedThisFrame);

    void createGroupObjects();

    //Result of WMO fog query
    struct WmoFogBlendResult {
        bool fogFound = false;          // a WMO fog record applies at the camera position
        bool insideInterior = false;    // camera is inside a non-exterior(-lit) group of this WMO
        float distToExit = 0.0f;        // distance from camera to nearest portal of the interior group(s)
        SMOFog fog = {};                // blended WMO fog record (fog + underwater_fog)
    };
    void checkFog(const mathfu::vec3 &cameraPos, int currentGroupIndex, WmoFogBlendResult &result);

    bool doPostLoad(const HMapSceneBufferCreate &sceneRenderer);
    void update();
    void uploadGeneratorBuffers();

    void createMaterialCache();
    void updateBB() override ;

    CAaBox getAABB();

    bool hasAlwaysDrawGroups() const { return m_hasAlwaysDrawGroups; }
    bool hasAntiportalGroups() const { return m_hasAntiportalGroups; }

    const CAaBox &getGroupWorldBorder(int groupId) override { return m_groupWorldBorders[groupId]; }
    const CAaBox &getGroupVolumeWorldBorder(int groupId) override { return m_groupVolumeWorldBorders[groupId]; }
    void recalcGroupBorders(int groupId) override;

    //A group that loads before the camera turns to it: one open to the outside or drawn unconditionally
    bool isGroupPreloaded(int groupId);
    //Queues the not-yet-loaded preloaded groups for loading without doing any culling work.
    //Used when the whole WMO was culled at the WMO bbox level.
    void triggerExteriorGroupLoads(WMOGroupListContainer &wmoGroupArray);

    //A creature standing at the position, which may be inside one of this WMO's interior groups. The WMO decides once the
    //groups holding the position are loaded (homeCreatures): in an interior group it hands the creature to the group, where
    //it draws with the group's doodads, and otherwise it declines.
    void offerCreature(const std::shared_ptr<PlacedCreature> &creature, const mathfu::vec3 &position);
    //Queues the groups the offered creatures wait for, so they load though no view reaches them
    void queueCreatureGroupLoads(WMOGroupListContainer &wmoGroupArray);
    //Answers the offered creatures if the WMO is loaded and an offer or a group load came since the last answer
    void homeCreaturesIfChanged();

public:
    //Adds every group the frustum reaches to the view's draw, doodad and load lists; true when one draws
    bool collectGroups(
        mathfu::vec4 &cameraVec4,
        const MathHelper::FrustumCullingData &frustumData,
        GeneralView &exteriorView
    );

    void checkGroupDoodads(
        int groupId,
        mathfu::vec4 &cameraVec4,
        std::vector<mathfu::vec4> &frustumPlanes,
        M2ObjectListContainer &m2Candidates);

    bool getGroupWmoThatCameraIsInside(mathfu::vec4 cameraVec4, WmoGroupResult &result, float &bottomBorder);

    bool isGroupWmoInterior(int groupId);
    // True for a group the config leaves out: UNREACHABLE with skipUnreachableWmoGroups, or named alone by modelBlock. The caller
    // holds modelBlock's lock.
    bool isGroupSkipped(int groupId);
    // Counts the groups a bake of the square from min to max (world x and y) draws: those the config leaves in whose box the square
    // reaches. `pending` is how many of `total` have not loaded.
    void countBakeGroups(float minX, float maxX, float minY, float maxY, int &total, int &pending);
    bool isGroupWmoExteriorLit(int groupId);
    bool isGroupWmoExtSkybox(int groupId);

    void createWorldPortals();
    void createNewLights();
    void calculateAmbient();
    std::shared_ptr<CEngineLight> getNewLight(int index) override;
    void setInteriorAmbientColor(int groupIndex,
        bool isExteriorLighted,
        const mathfu::vec3 &ambient,
        const mathfu::vec3 &horizontAmbient,
        const mathfu::vec3 &groundAmbient
    ) override;
};

typedef EntityFactory<2000, WMOObjId, WmoObject, CAaBox> WMOEntityFactory;

extern std::shared_ptr<WMOEntityFactory> wmoFactory;

static const CAaBox wmoNonexistingAabb = CAaBox(
    mathfu::vec3_packed(mathfu::vec3(999999, 999999, 999999)),
    mathfu::vec3_packed(mathfu::vec3(-999999, -999999, -999999))
);

template<>
inline const CAaBox &retrieveAABB<>(const WMOObjId &objectId) {
    auto * ptr = wmoFactory->getObjectByIdConst<1>(objectId);
    return ptr ? *ptr : wmoNonexistingAabb;
}

class WMOListContainer {
    using wmoContainer = framebased::vector<WMOObjId>;
//    using wmoContainer = std::vector<std::shared_ptr<WmoObject>>;
private:
    wmoContainer wmoCandidates;
    wmoContainer wmoToLoad;
    wmoContainer wmoToDrawn;

    bool candCanHaveDuplicates = false;
    bool toLoadCanHaveDuplicates = false;
    bool toDrawmCanHaveDuplicates = false;

    bool m_locked = false;

    void inline removeDuplicates(wmoContainer &array) {
        if (array.size() < 1000) {
            std::sort(array.begin(), array.end(), [](auto &a, auto &b) -> bool {
                return a < b;
            });
        } else {
            tbb::parallel_sort(array.begin(), array.end(), [](auto &a, auto &b) -> bool {
                return a < b;
            });
        }
        array.erase(std::unique(array.begin(), array.end()), array.end());
        return;
    }

public:
    WMOListContainer() {
        wmoCandidates.reserve(3000);
        wmoToLoad.reserve(3000);
        wmoToDrawn.reserve(3000);
    }

    void addCand(const std::shared_ptr<WmoObject> &toDraw) {
        if (m_locked) {
            throw "oops";
        }

        if (toDraw->isLoaded()) {
            wmoCandidates.push_back(toDraw->getObjectId());
            candCanHaveDuplicates = true;
        } else {
            if (toDraw->isFailedToLoadGeom())
                return;

            wmoToLoad.push_back(toDraw->getObjectId());
            toLoadCanHaveDuplicates = true;
        }
    }

    void addToLoad(const std::shared_ptr<WmoObject> &toLoad) {
        if (m_locked) {
            throw "oops";
        }

        if (!toLoad->isLoaded()) {
            wmoToLoad.push_back(toLoad->getObjectId());
            toLoadCanHaveDuplicates = true;
        }
    }

    void addToDrawn(const std::shared_ptr<WmoObject> &toDrawn) {
        if (m_locked) {
            throw "oops";
        }

        if (toDrawn->isLoaded()) {
            wmoToDrawn.push_back(toDrawn->getObjectId());
            toDrawmCanHaveDuplicates = true;
        }
    }
    void addToDrawn(WmoObject* toDrawn) {
        if (m_locked) {
            throw "oops";
        }

        if (toDrawn->isLoaded()) {
            wmoToDrawn.push_back(toDrawn->getObjectId());
            toDrawmCanHaveDuplicates = true;
        }
    }

    const wmoContainer &getCandidates() {
        if (this->candCanHaveDuplicates) {
            removeDuplicates(wmoCandidates);
            candCanHaveDuplicates = false;
        }

        return wmoCandidates;
    }

    const wmoContainer &getToLoad() {
        if (this->toLoadCanHaveDuplicates) {
            removeDuplicates(wmoToLoad);
            toLoadCanHaveDuplicates = false;
        }

        return wmoToLoad;
    }


    const wmoContainer &getToDrawn() {
        if (this->toDrawmCanHaveDuplicates) {
            removeDuplicates(wmoToDrawn);
            toDrawmCanHaveDuplicates = false;
        }

        return wmoToDrawn;
    }

    void lock() {
        getToDrawn();
        getToLoad();
        getCandidates();

        m_locked = true;
    }

};


#endif //WEBWOWVIEWERCPP_WMOOBJECT_H
