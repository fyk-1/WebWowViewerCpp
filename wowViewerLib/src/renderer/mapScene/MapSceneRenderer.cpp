//
// Created by Deamon on 11/26/2022.
//

#include "MapSceneRenderer.h"
#include "../../engine/objects/scenes/map.h"
#include "../frame/FrameProfile.h"

std::shared_ptr<MapRenderPlan>
MapSceneRenderer::processCulling(const std::shared_ptr<FrameInputParams<MapSceneParams>> &frameInputParams) {
//    for (auto &scene : scenes) {
        auto mapScene = std::dynamic_pointer_cast<Map>(frameInputParams->frameParameters->scene);
        auto mapPlan = std::make_shared<HMapRenderPlan::element_type>();
        mapScene->makeFramePlan(*frameInputParams, mapPlan);
//    }

    return mapPlan;
}

void MapSceneRenderer::collectViewMeshes(const std::shared_ptr<MapRenderPlan> &renderPlan,
                                         COpaqueMeshCollector &opaqueMeshCollector,
                                         framebased::vector<HGSortableMesh> &transparentMeshes,
                                         bool includeWmoTransparents) {
    ZoneScoped;

    bool renderADT = m_config->renderAdt;
    bool renderWMO = m_config->renderWMO;

    {
        auto exteriorView = renderPlan->viewsHolder.getExterior();
        if (exteriorView != nullptr) {
            ZoneScopedN("Collect Exterior");
            exteriorView->collectMeshes(renderADT, true, renderWMO, opaqueMeshCollector, transparentMeshes, includeWmoTransparents);

            {
                ZoneScopedN("adt mesh collect");
                for (auto &adtRes: renderPlan->adtArray) {
                    adtRes.adtObject->collectMeshes(adtRes, opaqueMeshCollector, transparentMeshes);
                }
            }
        }
    }
}

void MapSceneRenderer::collectM2ProjectiveMeshes(const std::shared_ptr<MapRenderPlan> &renderPlan,
                                                 COpaqueMeshCollector &opaqueMeshCollector) {
    // Decal/projective meshes aren't part of the GPU-indirect draw path, so they are
    // collected here on the CPU.
    if (!m_config->renderM2 || !m_config->renderM2Decals) return;

    ZoneScopedN("collect m2 decals");
    for (auto m2ObjectId : renderPlan->m2Array.getDrawn()) {
        auto m2Object = m2Factory->getObjectById<0>(m2ObjectId);
        if (m2Object == nullptr) continue;
        m2Object->collectProjectiveMeshes(opaqueMeshCollector);
    }
}

void MapSceneRenderer::collectSkyMeshes(const std::shared_ptr<MapRenderPlan> &renderPlan,
                                        framebased::vector<HGMesh> &skyMeshes) {
    auto skyBoxView = renderPlan->viewsHolder.getSkybox();
    if (skyBoxView) {
        ZoneScopedN("collect skyBox");

        skyBoxView->collectMeshes(skyMeshes);
    }
}

void MapSceneRenderer::updateSceneWideChunk(const std::shared_ptr<IBufferChunkVersioned<sceneWideBlockVSPS>> &sceneWideChunk,
                                            const std::vector<RenderingMatAndSceneSize> &renderingMatricesAndSizes,
                                            const HFrameDependantData &fdd,
                                            bool isVulkan,
                                            animTime_t sceneTime,
                                            const MapRenderPlan &plan
                                            ) {
    ZoneScoped;

    auto safeInv = [](float x) {
        return feq(x, 0) ? 0 : 1.0f / x;
    };

    const static mathfu::vec4 zUp = {0,0,1.0,0};

    if (fdd->fogResults.empty()) fdd->fogResults.emplace_back();

    for (int i = 0; i < renderingMatricesAndSizes.size(); i++) {
        auto const &matAndSceneSize = renderingMatricesAndSizes[i];
        auto const &renderingMatrices = matAndSceneSize.renderingMat;

        auto &blockPSVS = sceneWideChunk->getObject(i);
        blockPSVS.uLookAtMat = renderingMatrices->lookAtMat;
        blockPSVS.uInvLookAtMat = renderingMatrices->lookAtMat.Inverse();
        if (isVulkan) {
            auto perspMat = MathHelper::getVulkanMat4Fix() * renderingMatrices->perspectiveMat;
            if (matAndSceneSize.jitterX != 0.0f || matAndSceneSize.jitterY != 0.0f) {
                // The jitter as a shift of NDC, applied after the projection so the camera classes never see it.
                // Culling takes its own unjittered matrix.
                perspMat = mathfu::mat4::FromTranslationVector(mathfu::vec3(
                    2.0f * matAndSceneSize.jitterX / (float) matAndSceneSize.width,
                    2.0f * matAndSceneSize.jitterY / (float) matAndSceneSize.height,
                    0.0f)) * perspMat;
            }
            blockPSVS.uPMatrix = perspMat;
            blockPSVS.uInvPMatrix = perspMat.Inverse();
        } else {
            blockPSVS.uPMatrix = renderingMatrices->perspectiveMat;
            blockPSVS.uInvPMatrix = renderingMatrices->perspectiveMat.Inverse();
        }

        blockPSVS.uInteriorSunDir = mathfu::vec4_packed(
            mathfu::vec4(renderingMatrices->interiorDirectLightDir.xyz(), 0)
        );
        blockPSVS.uSceneSize_DisableLightBuffer = mathfu::vec4_packed(mathfu::vec4(
            (float)matAndSceneSize.width,
            (float)matAndSceneSize.height,
            !m_config->enableLightBuffer ? 1.0 : 0.0,
            (float)(m_config->woweeShaders | (m_config->bakeUnlit ? 32 : 0) | (fdd->tileLight != nullptr ? 64 : 0))
        ));

        blockPSVS.uViewUpSceneTime = mathfu::vec4(renderingMatrices->viewUp.xyz(), sceneTime);

        const auto &lc = fdd->liquidColors;
        blockPSVS.closeOceanColor = mathfu::vec4(lc.closeOceanColor, lc.oceanShallowAlpha);
        blockPSVS.farOceanColor =   mathfu::vec4(lc.farOceanColor, lc.oceanDeepAlpha);
        blockPSVS.closeRiverColor = mathfu::vec4(lc.closeRiverColor, lc.riverShallowAlpha);
        blockPSVS.farRiverColor =   mathfu::vec4(lc.farRiverColor, lc.riverDeepAlpha);

        blockPSVS.extLight.uExteriorAmbientColor         = mathfu::vec4(fdd->colors.exteriorAmbientColor, 1.0);
        blockPSVS.extLight.uExteriorHorizontAmbientColor = mathfu::vec4(fdd->colors.exteriorHorizontAmbientColor, 1.0);
        blockPSVS.extLight.uExteriorGroundAmbientColor   = mathfu::vec4(fdd->colors.exteriorGroundAmbientColor, 1.0);
        blockPSVS.extLight.uExteriorDirectColor          = mathfu::vec4(fdd->colors.exteriorDirectColor, 1.0);
        blockPSVS.extLight.uExteriorDirectColorDir       = mathfu::vec4(fdd->exteriorDirectColorDir, 1.0);
        blockPSVS.extLight.uExteriorSpecularColor        = mathfu::vec4(fdd->colors.exteriorSpecularColor, 1.0);
        blockPSVS.extLight.uSunPosition                  = mathfu::vec4(fdd->sunPos, 1.0);
        blockPSVS.extLight.uSunAttenuation               = mathfu::vec4(
            fdd->sunAttentuationStart,
            safeInv(fdd->sunAttentuationEnd - fdd->sunAttentuationStart),
            fdd->useSunAttenuation ? 1.0 : 0.0,
            1.0f
        );
        blockPSVS.extLight.uAdtSpecMult_FogCount         = mathfu::vec4(m_config->adtSpecMult, fdd->fogResults.size(), 0, 1.0);
        blockPSVS.uWoweeLook = mathfu::vec4(m_config->skyGlow ? 1.0f : 0.0f, m_config->waterSkyReflection ? 1.0f : 0.0f,
                                           std::clamp(m_config->foliageTintStrength, 0.0f, 2.0f), m_config->farPlaneForCulling);
        if (m_config->waterSkyReflection) {
            blockPSVS.uSkyTopColor    = mathfu::vec4(fdd->skyColors.SkyTopColor, 0.0f);
            blockPSVS.uSkyMiddleColor = mathfu::vec4(fdd->skyColors.SkyMiddleColor, 0.0f);
            blockPSVS.uSkyBand1Color  = mathfu::vec4(fdd->skyColors.SkyBand1Color, 0.0f);
            blockPSVS.uSkyBand2Color  = mathfu::vec4(fdd->skyColors.SkyBand2Color, 0.0f);
        }

        for (int i = 0; i < std::min<int>(fdd->fogResults.size(), FOG_MAX_SHADER_COUNT); i++) {
            auto &fogResult = fdd->fogResults[i];

            auto &fogData = blockPSVS.fogData;

            mathfu::vec4 heightPlane = mathfu::vec4(
                zUp.xyz(),
                -(mathfu::vec3::DotProduct(zUp.xyz(), (zUp * fogResult.FogHeight).xyz()))
            );
            heightPlane = renderingMatrices->invTranspViewMat * heightPlane;

            float fogEnd = std::min<float>(std::max<float>(m_config->farPlane, 277.5), m_config->farPlane);
            const float densityMultFix = 0.00050000002 * std::pow(10, m_config->fogDensityIncreaser);
            float fogScaler = fogResult.FogScaler;
            // if (fogScaler <= 0.00000001f) fogScaler = 0.5f;
            float fogStart = std::min<float>(m_config->farPlane, 3000) * fogScaler;

            if (m_config->disableFog || !fdd->FogDataFound) {
                fogStart = 0.0f;
                fogEnd = 100000000.0f;
                fogScaler = 0;
                fogResult.EndFogColorDistance = 100000000.0f;
                fogResult.MainFogEndDist = 100000000.0f;
                fogResult.FogStartOffset = 100000000.0f;
                fogResult.MainFogStartDist = 0;
                fogResult.FogDensity = 0;
                fogResult.FogHeightDensity = 0;
            }

            fogData.densityParams = mathfu::vec4(
                fogStart,
                fogEnd,
                fogResult.FogDensity * densityMultFix,
                0);
            fogData.classicFogParams = mathfu::vec4(0, 0, 0, 0);
            if (m_config->classicFog && !m_config->disableFog && fdd->FogDataFound) {
                // Legacy fog data: fog rising from the ground. Its density at ground level covers 90 % at the zone's
                // FogEnd. The shader thins it with the camera's height over the ground, carried in densityParams.w, which classic mode leaves
                // unread. The ground is classicFogGroundZ where a script set it, else the plan's terrain height, else the WDL's, else the camera's
                // own z (full density).
                float zoneEnd = std::max(fogResult.FogEnd, 1.0f);
                float groundDensity = std::log(10.0f) / zoneEnd;
                // robots@Impl: The shader thins the fog by e every w yards, and the config names the percent kept at a height over the ground
                // (classicFogKept at classicFogKeptAt), so w is that height over -ln(kept / 100).
                const float eFoldingHeight = std::max(m_config->classicFogKeptAt, 1.0f) / -std::log(std::clamp(m_config->classicFogKept, 1.0f, 99.0f) / 100.0f);
                fogData.classicFogParams = mathfu::vec4(2.0f, groundDensity, 0.0f, eFoldingHeight);
                const float cameraZ = renderingMatrices->cameraPos.z;
                const float scriptGround = m_config->classicFogGroundZ;
                const float ground = scriptGround > -99999.0f ? scriptGround : plan.hasGroundHeight ? plan.groundHeight
                                   : plan.hasWdlGround ? plan.wdlGround : cameraZ;
                fogData.densityParams.w = std::max(cameraZ - ground, 0.0f);
            }
            fogData.heightPlane = heightPlane;
            fogData.color_and_heightRate = mathfu::vec4(fogResult.FogColor, fogResult.FogHeightScaler
    //            * 0.01
            );
            fogData.heightDensity_and_endColor = mathfu::vec4(
                fogResult.FogHeightDensity * densityMultFix,
                fogResult.EndFogColor.x,
                fogResult.EndFogColor.y,
                fogResult.EndFogColor.z
            );

            fogData.sunAngle_and_sunColor = mathfu::vec4(
                fogResult.SunFogAngle,
                fogResult.SunFogColor.x,
                fogResult.SunFogColor.y,
                fogResult.SunFogColor.z
            );
            fogData.heightColor_and_endFogDistance = mathfu::vec4(
                fogResult.FogHeightColor,
                (fogResult.EndFogColorDistance > 0) ?
                fogResult.EndFogColorDistance :
                1000.0f
            );
            // The far fog (commonFogFunctions.slang, makeFog2): the sky cone's fog colour, which the land's edge meets, and its rate per yard,
            // 0 with the far fog off.
            const bool farFogOn = m_config->farTerrain && !m_config->disableFog && fdd->FogDataFound && plan.farTerrain.extent > 0.0f;
            const float farRate = farFogOn ? std::max(m_config->farTerrainFog, 0.0f) / plan.farTerrain.extent : 0.0f;
            fogData.sunPercentage_sunFogStrength = mathfu::vec4(
                fogResult.SunAngleBlend * fogResult.SunFogStrength,
                fogResult.SunFogStrength,
                1.0f, 1.0f);
            fogData.sunDirection_and_fogZScalar = mathfu::vec4(
                fdd->sunDirection,
                fogResult.FogZScalar
            );
            fogData.heightFogCoeff = fogResult.FogHeightCoefficients;
            fogData.mainFogCoeff = fogResult.MainFogCoefficients;
            fogData.heightDensityFogCoeff = fogResult.HeightDensityFogCoefficients;

            bool mainFogOk = (fogResult.MainFogStartDist + 0.001 <= fogResult.MainFogEndDist);
            fogData.mainFogEndDist_mainFogStartDist_legacyFogScalar_blendAlpha = mathfu::vec4(
                mainFogOk ? fogResult.MainFogEndDist : fogResult.MainFogStartDist + 0.001,
                fogResult.MainFogStartDist >= 0.0 ? fogResult.MainFogStartDist : 0.0f,
                fogResult.LegacyFogScalar,
                1.0f//fogResult.FogBlendAlpha
            );
            fogData.heightFogEndColor_fogStartOffset = mathfu::vec4(
                fogResult.HeightEndFogColor,
                fogResult.FogStartOffset
            );
            fogData.farFogColor_rate = mathfu::vec4(fdd->skyColors.SkyFogColor, farRate);
        }

        // Underwater fog (Light.db2 LightParams slot 1) for liquid above shaders.
        // Same density unit conversion as the scene fog; classic params unused (0).
        {
            auto &uwFog = fdd->underWaterFogResult;
            const float uwDensityMultFix = 0.00050000002 * std::pow(10, m_config->fogDensityIncreaser);
            if (uwFog.FogEnd > 0.0f) {
                blockPSVS.underWaterFog = mathfu::vec4(
                    uwFog.FogEnd * uwFog.FogScaler,
                    uwFog.FogEnd,
                    uwFog.FogDensity * uwDensityMultFix,
                    0.0f
                );
                blockPSVS.underWaterFogColor = mathfu::vec4(uwFog.FogColor, 0.0f);
            } else {
                // no data -> inert fog
                blockPSVS.underWaterFog = mathfu::vec4(0.0f, 100000000.0f, 0.0f, 0.0f);
                blockPSVS.underWaterFogColor = mathfu::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }
            blockPSVS.underWaterClassicFogParams = mathfu::vec4(0, 0, 0, 0);
        }
        sceneWideChunk->saveVersion(i);
    }
}
