//
// Created by deamon on 22.10.19.
//

#ifndef AWEBWOWVIEWERCPP_VIEWSOBJECTS_H
#define AWEBWOWVIEWERCPP_VIEWSOBJECTS_H

#include <memory>

class SkyView;
class GeneralView;
class ExteriorView;
class ADTObjRenderRes;
class FrameViewsHolder;

typedef std::shared_ptr<GeneralView> HGeneralView;
typedef std::shared_ptr<ExteriorView> HExteriorView;
typedef std::shared_ptr<SkyView> HSkyView;

#include <vector>
#include "wmo/wmoGroupObject.h"
#include "m2/m2Object.h"

class ADTObjRenderRes {
public:
    std::shared_ptr<AdtObject> adtObject;
    std::array<bool, 256> drawChunk = {false};
    std::array<bool, 256> drawWaterChunk = {false};
    // For a tile still loading: whether its square meets the view, so Map::doPostLoad finishes it before those that do not.
    bool inView = true;
};



class GeneralView {
public:
    WMOGroupListContainer wmoGroupArray;
    M2ObjectListContainer m2List;

    std::vector<std::vector<mathfu::vec3>> worldAntiPortalVertices = {};
    MathHelper::FrustumCullingData frustumData;

    int renderOrder = -1;
    struct PortalPointsFrame {
        HGIndexBuffer m_indexVBO;
        HGVertexBuffer m_bufferVBO;

        HGVertexBufferBindings m_bindings;
    };
    framebased::vector<HGSortableMesh> m_portalMeshes = {};

    std::vector<PortalPointsFrame> portals;

    virtual void collectMeshes(bool renderADT, bool renderAdtLiquid, bool renderWMO, COpaqueMeshCollector &opaqueMeshCollector, framebased::vector<HGSortableMesh> &transparentMeshes, bool includeWmoTransparents = true);
    virtual void collectLights(std::vector<LocalLight> &pointLights, std::vector<SpotLight> &spotLights, std::vector<std::shared_ptr<CEngineLight>> &newWmoLights);
    void collectPortalMeshes(framebased::vector<HGSortableMesh> &transparentMeshes);

    void produceTransformedPortalMeshes(const HMapSceneBufferCreate &sceneRenderer, const HApiContainer &apiContainer,
                                        const std::vector<std::vector<mathfu::vec3>> &portalsVerts, bool isAntiportal = false);
    // Culls the view's candidates, its groups' doodads among them, by the frustum and by config's minM2ScreenPercent and minM2WorldSize.
    void addM2FromGroups(const MathHelper::FrustumCullingData &frustumData, mathfu::vec4 &cameraPos, const Config *config);
};

class ExteriorView : public GeneralView {
public:
    void collectMeshes(bool renderADT, bool renderAdtLiquid, bool renderWMO, COpaqueMeshCollector &opaqueMeshCollector, framebased::vector<HGSortableMesh> &transparentMeshes, bool includeWmoTransparents = true) override;
};

class SkyView {
public:
    M2ObjectListContainer m2List;
    M2ObjectListContainer stars;

    HGMesh skyMesh = nullptr;
    HGMesh skyMesh0x4 = nullptr;

    std::array<HGMesh, 3> m_planetMeshes = {nullptr, nullptr, nullptr};

    void collectMeshes(framebased::vector<HGMesh> &meshes);
};

class FrameViewsHolder {
public:
    HExteriorView getOrCreateExterior(const MathHelper::FrustumCullingData &frustumData);
    HExteriorView getExterior();
    HSkyView getSkybox();
private:
    HExteriorView exteriorView = nullptr ;
    HSkyView skyBoxView = nullptr ;

};


#endif //AWEBWOWVIEWERCPP_VIEWSOBJECTS_H
