//
// Created by deamon on 15.06.17.
//

#ifndef WOWVIEWERLIB_CONFIG_H
#define WOWVIEWERLIB_CONFIG_H

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mathfu/glsl_mappings.h>
#include "../renderer/mapScene/FrameDependentData.h"

constexpr float DEFAULT_FOV_VALUE = 53.9726294579437f;

struct RiverColorOverride {
    int liquidObjectId;
    int liquidType;
    mathfu::vec4 color = {0,0,0,0};
};

typedef std::vector<RiverColorOverride> RiverColorOverrideHolder;
typedef std::shared_ptr<RiverColorOverrideHolder> HRiverColorOverrideHolder;

enum class EParameterSource: char {
    eNone,
    eConfig,
    eDatabase,
    eM2
};

enum class EFreeStrategy : char {
    eTimeBased,
    eFrameBase
};

struct ConfigWMOVertFlags {
    uint32_t useMOCV: 1;
};

class Config {
public:
    Config() {
        m_hardwareThreadCount = std::max<int>((int)std::thread::hardware_concurrency()-3, 1);
    }

public:
    auto hardwareThreadCount() const -> int { return m_hardwareThreadCount;}
    bool renderAdt = true;
    bool renderWMO = true;
    bool renderM2 = true;
    bool renderM2Decals = true;
    bool renderRibbons = true;
    bool renderSkyDom = true;
    bool renderSkyScene = true;
    // Player conditions (from the SkySceneXPlayerCondition db2) that the user turned
    // off; WDL sky scenes gated by these are not shown. Empty = everything enabled.
    std::unordered_set<int> disabledSkyScenePlayerConditions;
    bool renderLiquid = true;
    bool renderGameObjects = true;
    bool showGameObjectNameplates = false;
    bool showGameObjectNameplatesForAll = false;
    bool renderBSP = false;
    bool renderPortals = false;
    bool renderAntiPortals = false;
    // Leaves out WMO groups flagged UNREACHABLE. Stormwind's group 86 is one, a low-poly shell drawn always (ALWAYSDRAW) that
    // hangs over the city up to 277 yards, where nothing of the real city reaches.
    bool skipUnreachableWmoGroups = true;
    bool renderPortalsIgnoreDepth = false;
    bool usePortalCulling = true;
    bool discardInvisibleMeshes = true;

    bool ignoreADTHoles = false;
    bool ignoreADTColoring = false;
    bool ignoreWMOColoring = false;

    bool renderAllWmoDoodads = false;

    bool stopBufferUpdates = false;
    bool stepBufferUpdate = false;


    bool disableFog = false;


    bool drawDebugLights = false;
    bool drawWmoBB = false;
    bool drawM2BB = false;

    bool enableObjectPicking = true;

    bool enableFXAA = true;

    bool doubleCameraDebug = false;
    bool controlSecondCamera = false;
    bool swapMainAndDebug = false;

    // bool BCLightHack = false;

    bool enableLightBuffer = true;

    bool drawDepthBuffer = false;
    int cameraM2 = -1; // this will be sceneNumber of object

    int wmoMinBatch = 0;
    int wmoMaxBatch = 9999;

    int m2MinBatch = 0;
    int m2MaxBatch = 9999;

    int minParticle = 0;
    int maxParticle = 9999;

    uint16_t currentTime = 2880/2; //midday is the default time now
    float timeMultiplier = 1.0f;

    bool useWotlkLogic = false;

    float fov = DEFAULT_FOV_VALUE;
    float nearPlane = 1;
    float farPlane = 1000;
    float farPlaneForCulling = 400;
    // The draw distance with the camera in the air; farPlaneForCulling is the one on the ground (cameraGrounded).
    float farPlaneForCullingAir = 1500;
    // Yards ahead on a camera's route whose map tiles are loaded before they are seen. 0 or less turns it off.
    // Only a camera that knows its route answers it (the flyover path camera).
    float preloadAhead = 1200;
    // Frames per second the main loop holds to at most. 0 or less leaves it uncapped.
    float frameRateCap = 0;
    // Scale of the scene against the window, 0.05 to 1.5, stretched or shrunk to the window by the final pass.
    // Object picking in the UI mode assumes 1. With fsrUpscaling on and the scale at most 1, FSR upscales it instead;
    // AMD supports ratios down to 1/3, and below that the image degrades.
    float renderScale = 1;
    // Upscales the scene to the window with AMD FSR 3.1 instead of stretching it. Needs the bindless renderer and
    // amd_fidelityfx_vk.dll beside the executable; without either the scene is stretched as before.
    bool fsrUpscaling = false;
    // RCAS sharpening of the FSR output, 0 to 1.
    float fsrSharpness = 0.3f;
    // Size FSR upscales to, as a fraction of the window, 0.25 to 1; the last pass stretches its output to the window.
    // Below 1 FSR costs less, and renderScale above it turns FSR off.
    float fsrOutputScale = 1;
    // Skips a model (M2) whose bounding sphere spans less of the view's height than this, in percent: small props
    // drop out at a distance and large ones stay. Doodads inside WMOs are not affected. Never below
    // MinM2ScreenPercentFloor: with a long draw distance, culling nothing loads the CPU and GPU with every far prop.
    static constexpr float MinM2ScreenPercentFloor = 1;
    float minM2ScreenPercent = 1;
    // The camera counts as on the ground up to groundAltitude yards above the terrain, and in the air above it.
    // minM2ScreenPercent applies on the ground and minM2ScreenPercentAir in the air.
    float groundAltitude = 30;
    float minM2ScreenPercentAir = 2;
    // Set by the map every frame from cameraAltitude and groundAltitude.
    bool cameraGrounded = true;

    float effectiveFarPlaneForCulling() const {
        return cameraGrounded ? farPlaneForCulling : farPlaneForCullingAir;
    }
    float effectiveMinM2ScreenPercent() const {
        const float ground = minM2ScreenPercent > MinM2ScreenPercentFloor ? minM2ScreenPercent : MinM2ScreenPercentFloor;
        const float air = minM2ScreenPercentAir > MinM2ScreenPercentFloor ? minM2ScreenPercentAir : MinM2ScreenPercentFloor;
        return cameraGrounded ? ground : air;
    }
    // Which WoWee shader ports draw, as a bitmask: 1 water, 2 terrain, 4 WMO, 8 M2, 16 magma and slime. The rest of each
    // family keeps the retail shading. Reaches the shaders in the scene-wide block's uSceneSize_DisableLightBuffer.w.
    int woweeShaders = 17;
    // Set by the host when the camera jumps, so the next FSR dispatch drops its history. The renderer clears it.
    bool fsrResetRequested = false;

    float fogDensityIncreaser = 0;
    // Set when the database opens, from IClientDatabase::usesClassicFog: the map renderer then draws fog that rises from
    // the ground, densest at the terrain under the camera and thinning by a factor of e every classicFogFalloff yards
    // above it. At classicFogStrength 1 the ground density is the game's: 90 % cover at the zone's FogEnd.
    bool classicFog = false;
    float classicFogStrength = 1;
    float classicFogFalloff = 60;
    // Set by the map every frame: the camera's height above the terrain under it, averaged over several seconds, in yards.
    float cameraAltitude = 0;

    bool disableGlow = false;

    bool pauseAnimation = false;

    // When true (and supported by the device/renderer), M2 skeletal animation,
    // particle emitters and ribbon emitters are evaluated on GPU (Vulkan compute)
    // instead of CPU. Per-object fallback to CPU happens automatically.
    bool useGpuAnimation = true;

    mathfu::vec4 clearColor = {0.117647, 0.207843, 0.392157, 0};

    EParameterSource globalLighting = EParameterSource::eDatabase;
    ExteriorColors exteriorColors;
    mathfu::vec3 exteriorDirectColorDir;

    float adtSpecMult = 1.0;

    bool useMinimapWaterColor = false;
    bool useCloseRiverColorForDB = false;
    EParameterSource waterColorParams = EParameterSource::eDatabase;
    LiquidColors liquidColors;

    EParameterSource skyParams = EParameterSource::eDatabase;
    SkyColors skyColors;

    EParameterSource globalFog = EParameterSource::eDatabase;
    FogResult fogResult;

    int diffuseColorHack = 0;

    EParameterSource glowSource = EParameterSource::eDatabase;
    float currentGlow = 0;

    //Culling preferences
    double adtTTLWithoutUpdate = 10000; //10 secs by default
    double adtFTLWithoutUpdate = 6; //25 frames by default
    EFreeStrategy adtFreeStrategy = EFreeStrategy::eTimeBased;

    HRiverColorOverrideHolder colorOverrideHolder = nullptr;
private:
    int m_hardwareThreadCount;
};

//ADT STUFF FOR MAP GENERATION
typedef std::array<int,2> AdtCell;

struct AdtCellCompare
{
    bool operator()(const AdtCell &a, const AdtCell &b) const {
        return a[0] == b[0] && a[1] == b[1];
    }
};

struct AdtCellHasher {
    std::size_t operator()(const AdtCell &k) const {
        using std::hash;
        return hash<int>{}(k[0]) ^ (hash<int>{}(k[1]) << 16);
    };
};

struct ADTRenderConfigData {
    ADTRenderConfigData() {
        std::array<mathfu::vec3, 64> defaultMax = {};
        std::array<mathfu::vec3, 64> defaultMin = {};
        defaultMax.fill({-200000, -200000, -200000});
        defaultMin.fill({200000, 200000, 200000});

        adtMax.fill(defaultMax);
        adtMin.fill(defaultMin);
    }
    std::array<std::array<mathfu::vec3, 64>, 64> adtMin;
    std::array<std::array<mathfu::vec3, 64>, 64> adtMax;
    std::unordered_set<AdtCell, AdtCellHasher, AdtCellCompare> excludedADTs;
    std::unordered_map<AdtCell, std::unordered_set<AdtCell, AdtCellHasher, AdtCellCompare>, AdtCellHasher> excludedChunksPerADTs;
};

typedef std::shared_ptr<ADTRenderConfigData> HADTRenderConfigDataHolder;



#endif //WOWVIEWERLIB_CONFIG_H
