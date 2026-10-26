//
// Created by Deamon on 7/5/2024.
//

#ifndef AWEBWOWVIEWERCPP_DAYNIGHTLIGHTHOLDER_H
#define AWEBWOWVIEWERCPP_DAYNIGHTLIGHTHOLDER_H

#include <string>
#include <vector>
#include "../../../persistance/header/commonFileStructs.h"
#include "../../../ApiContainer.h"
#include "../../../../renderer/mapScene/MapScenePlan.h"
#include "../../../../../3rdparty/mathfu/include/mathfu/glsl_mappings.h"
#include "LightParamCalculate.h"
#include "TileLightGrid.h"


class DayNightLightHolder {
public:
    explicit DayNightLightHolder(const HApiContainer &api, int mapId);

private:

    struct SkyBoxCollector {
        public:
            SkyBoxCollector(const HApiContainer& api, std::vector<std::shared_ptr<M2Object>> &existingSkyBoxes) : m_existingSkyBoxes(existingSkyBoxes), m_api(api) {};

            void addSkyBox(StateForConditions &stateForConditions, const SkyBoxInfo &skyBoxInfo, float alpha);
            const std::vector<std::shared_ptr<M2Object>> &getNewSkyBoxes() const {
                return m_newSkyBoxes;
            }
            bool getOverrideValuesWithFinalFog() const  { return m_overrideValuesWithFinalFog; }

        private:
            HApiContainer m_api;

            bool m_overrideValuesWithFinalFog = false;
            std::vector<std::shared_ptr<M2Object>> &m_existingSkyBoxes;
            std::vector<std::shared_ptr<M2Object>> m_newSkyBoxes;
    };

    std::vector<mapInnerZoneLightRecord> m_zoneLights;
    std::vector<std::shared_ptr<M2Object>> m_exteriorSkyBoxes;


    HApiContainer m_api;
    int m_mapId = -1;
public:
    void loadZoneLights();
    // How the map answers the tile light: which tiles it has, and the ground under their centres. Set once by the map.
    void setTileLightSource(TileLightGrid::Source source) { m_tileLightSource = std::move(source); }
    void updateLightAndSkyboxData(const HMapRenderPlan &mapRenderPlan, MathHelper::FrustumCullingData &frustumData,
                                   StateForConditions &stateForConditions, const AreaRecord &areaRecord);

    void getLightResultsFromDB(mathfu::vec3 &cameraVec3, const Config *config,
                                float &glow,
                                SkyColors &skyColors,
                                SkyBodyData &skyBodyData,
                                ExteriorColors &exteriorColors,
                                FogResult &fogResult,
                                FogResult &underWaterFogResult,
                                LiquidColors &liquidColors,
                                SkyBoxCollector &skyBoxCollector,
                                StateForConditions *stateForConditions);

    const std::vector<std::shared_ptr<M2Object>> &getExteriorSkyBoxes() const {
        return m_exteriorSkyBoxes;
    }

    FogResult wmoFogDataToFogResult(const SMOFog_Data &wmoFogData, float farClip) const;
    static void blendWmoFogIntoFogResult(FogResult &fogResult, const FogResult &wmoFog, float wmoBlend, bool underwater);

private:
    bool m_mapFlag2_0x2 = false;
    bool m_useWeightedBlend = false;
    bool m_mapHasFlag_0x200000 = false;
    bool m_mapHasFlag_0x10000 = false;

    float m_minFogDist1 = 0.0;
    float m_minFogDist2 = 0.0;
    float m_minFogDist3 = 0.0;

    void fixLightTimedData(LightTimedData &data, float farClip, float &fogScalarOverride);
    float getClampedFarClip(float farClip);
    void createMinFogDistances();

    // The light eased toward its target across frames, kept while Config::lightSmoothing is on.
    struct SmoothedLight {
        bool valid = false;
        float glow = 0.0f;
        ExteriorColors colors;
        SkyColors skyColors;
        LiquidColors liquidColors;
        FogResult fog;
        FogResult underWaterFog;
    } m_smoothedLight;

    void smoothLight(const HFrameDependantData &fdd, animTime_t deltaTime);

    // robots@Impl: The Light rows carry a storm light beside the clear one, in param slots 2 (scene) and 3 (underwater) where
    // slots 0 and 1 hold the clear pair. The frame's light is mix(clear, storm, m_stormShare), every blended quantity linearly. The share
    // eases toward Config::weatherIntensity while Config::weatherType is rain, snow or storm and toward 0 otherwise, with a time constant
    // of kStormEaseSeconds: the host rolls a zone's weather by thirds every ten minutes, and a jump of a third must not pop. The ease
    // runs whether or not Config::lightSmoothing is on, which smooths the mixed result after. A frame in which no time passed takes the
    // target as it is, as smoothLight does. A share of 0 leaves the storm light uncomputed, so a clear sky costs nothing more.
    static constexpr float kStormEaseSeconds = 3.0f;
    float m_stormShare = 0.0f;
    void easeStormShare(const Config *config, animTime_t deltaTime);

    // One complete light, the clear one or the storm one, before the two are mixed.
    struct LightSet {
        float glow = 0.0f;
        SkyColors skyColors;
        SkyBodyData skyBodyData;
        ExteriorColors exteriorColors;
        FogResult fog;
        FogResult underWaterFog;
        LiquidColors liquidColors;
    };

    // Blends the params of one weather into `set`, which holds the caller's defaults on entry. Each skybox it finds goes to the
    // collector at its blend times skyBoxAlphaScale.
    void computeLightSet(const std::vector<IdAndBlendAndPriority> &sceneBlends,
                         const std::vector<IdAndBlendAndPriority> &underwaterBlends,
                         const Config *config, LightSet &set, SkyBoxCollector &skyBoxCollector,
                         float skyBoxAlphaScale, StateForConditions *stateForConditions);

    // The per-tile light of Config::tileLight. Its blends are worked out a few tiles a frame, then the grid is coloured on the
    // frame the time of day changes and published to the frame data.
    TileLightGrid m_tileLight;
    TileLightGrid::Source m_tileLightSource;
    void updateTileLight(const HFrameDependantData &fdd, const Config *config);
    void colourTileLight(const Config *config);

    // What each light param of the grid gives the tiles at the time of day it was worked out for, kept so a change of the storm share
    // alone mixes the tiles again without asking the database.
    struct ParamColours {
        ExteriorColors exterior;
        LiquidColors liquid;
    };
    std::vector<ParamColours> m_tileParamColours;
    std::vector<ParamColours> m_tileStormParamColours;
    int m_tileParamColoursAt = -1;
    int m_tileStormParamColoursAt = -1;
    // The share the published grid was mixed with.
    float m_tileColouredShare = 0.0f;
    float tileStormShare() const;

    void
    inline calcLightParamResult(int lightParamId, const Config *config,
                                float &glow,
                                SkyBodyData &skyBodyData,
                                ExteriorColors &exteriorColors,
                                FogResult &fogResult, LiquidColors &liquidColors,
                                SkyColors &skyColors);

    void processSkyBoxes(const HMapRenderPlan &mapRenderPlan, const StateForConditions &stateForConditions,
                         const Config *config) const;

    std::array<mathfu::vec3, 4> calcPlanetPositions(const mathfu::vec3 &cameraVec3);

    // Computes per-frame planet (sun/moon discs) and stars render data into frameDependentData
    void updatePlanetsAndStars(const mathfu::vec3 &cameraPos, const SkyBodyData &skyBodyData,
                               const HFrameDependantData &fdd);

    mathfu::vec3 calcSunPosition(
        const SkyBodyData &skyBody,
        const mathfu::vec3 &cameraVec3,
        const mathfu::mat4 &viewMat,
        float &sunAttenuationStart,
        float &sunAttenuationEnd,
        std::array<mathfu::vec3, 4> &planetPositions
    );
    mathfu::vec3 calcDirectColorDir(const SkyBodyData &skyBody, const mathfu::mat4 &invTranspViewMat );
    mathfu::vec3 calcSunDirForFog(
        const SkyBodyData &skyBody,
        const mathfu::vec3 &sunPositionInView,
        const mathfu::vec3 &cameraVec3,
        const mathfu::mat4 &invTranspViewMat,
        std::array<mathfu::vec3, 4> &planetPositions
    );
};


#endif //AWEBWOWVIEWERCPP_DAYNIGHTLIGHTHOLDER_H
