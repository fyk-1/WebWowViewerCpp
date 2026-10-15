//
// Created by Deamon on 11.12.22.
//

#ifndef AWEBWOWVIEWERCPP_MAPSCENEPLAN_H
#define AWEBWOWVIEWERCPP_MAPSCENEPLAN_H

#include <vector>
#include "FrameDependentData.h"
#include "../../engine/objects/ViewsObjects.h"
#include "../../engine/objects/wmo/wmoObject.h"
#include "../../engine/objects/farTerrain/farTerrain.h"

struct MapRenderPlan {
    int adtAreadId = -1;
    int areaId = -1;
    int parentAreaId = -1;

    std::string areaName = "";
    std::string wmoAreaName = "";

    // The terrain height under the camera, in world z, where the camera stands over a loaded tile.
    bool hasGroundHeight = false;
    float groundHeight = 0;
    // The WDL's ground height under the camera, in world z, where the map's WDL has a tile there. Only while Config's farTerrain is on.
    bool hasWdlGround = false;
    float wdlGround = 0;
    // A tile the plan asked for has not loaded yet.
    bool tilesPending = false;

    animTime_t deltaTime;
    HCameraMatrices renderingMatrices;

    bool renderSky = false;

    //Result of culling test
    std::vector<WmoGroupResult> m_currentInteriorGroups = {};
    bool currentWmoGroupIsExtLit = false;
    bool currentWmoGroupShowExtSkybox = false;
    WMOObjId m_currentWMO = emptyWMO;
    int m_currentWmoGroup = -1;

    FrameViewsHolder viewsHolder;

    HFrameDependantData frameDependentData = std::make_shared<FrameDependantData>();

    //Objects for update and rendering
    std::vector<ADTObjRenderRes> adtArray = {};
    // Tiles loaded ahead of the camera. Loaded and kept alive, never updated or drawn.
    std::vector<std::shared_ptr<AdtObject>> adtPreload = {};

    // The far terrain of the frame, which the renderer draws after the opaque scene. Empty while Config's farTerrain is off.
    FarTerrainFrame farTerrain;

    // Set by Map::doPostLoad when the frame began: nothing the plan asked for was still loading and nothing waited to be finished.
    // The flyover mode reports it.
    bool loadSettled = false;

    // What a height bake waits on, set while Map::setBakeView is on (src/flyover/HeightBake.cpp): the camera the count was taken for, and
    // how many of the models and WMO groups the square around it draws have not loaded yet.
    struct BakeResidency {
        bool valid = false;
        float x = 0;
        float y = 0;
        int total = 0;
        int pending = 0;
        // Of those pending, the models, the WMOs and the WMO groups.
        int pendingBy[3] = {0, 0, 0};
    };
    BakeResidency bake;

    M2ObjectListContainer m2Array;
    WMOListContainer wmoArray;
    WMOGroupListContainer wmoGroupArray;

    std::vector<LocalLight> pointLights;
    std::vector<SpotLight> spotLights;
    std::vector<SpotLight> insideSpotLights;

    // Result of the async GPU object-id pick readback (right-click selection), decoded on the CPU.
    bool hasSelectedM2 = false;
    M2ObjId selectedM2 = (M2ObjId)0;

    WMOObjId selectedWMO = emptyWMO;
    int selectedWMOGroupNum = -1;

    bool hasSelectedAABB = false;
    CAaBox selectedAABB;

    // Result of a passive "hover peek" pick — same readback
    // mechanism as hasSelectedM2/selectedWMO above, but kept separate so hovering doesn't
    // change the actual selection (and therefore doesn't trigger the selection AABB highlight).
    bool hasHoveredM2 = false;
    M2ObjId hoveredM2 = (M2ObjId)0;

    WMOObjId hoveredWMO = emptyWMO;
    int hoveredWMOGroupNum = -1;
};
typedef std::shared_ptr<MapRenderPlan> HMapRenderPlan;
#endif //AWEBWOWVIEWERCPP_MAPSCENEPLAN_H
