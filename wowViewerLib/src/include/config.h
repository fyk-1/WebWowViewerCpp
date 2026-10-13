//
// Created by deamon on 15.06.17.
//

#ifndef WOWVIEWERLIB_CONFIG_H
#define WOWVIEWERLIB_CONFIG_H

#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mathfu/glsl_mappings.h>
#include "../renderer/mapScene/FrameDependentData.h"
#include "../renderer/mapScene/roadLines/RoadLines.h"

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
    // The host's CreatureSpawn rows, drawn as M2 models standing where they spawn.
    bool renderCreatures = true;
    // The humanoid NPCs among them, the ones dressed by a race and sex model. Off leaves them out while renderCreatures is on.
    bool renderHumanoids = true;
    // The host's GameObjectSpawn rows, drawn as M2 models standing where they spawn: herbs, ore veins, chests, mailboxes.
    bool renderGameObjectSpawns = true;
    // The ids of the world events whose spawns stand, as the host sends them. A spawn tied to a positive event (the Events column of
    // CreatureSpawn and GameObjectSpawn) draws only while one of its events is in the set, and one tied to a negative event is gone
    // while that event's id is in it. Empty draws the spawns no event ties and the ones that are gone only while an event runs. The culling
    // threads read the set while the command thread replaces it, so both go through activeSpawnEventsLock.
    std::unordered_set<int> activeSpawnEvents;
    mutable std::shared_mutex activeSpawnEventsLock;
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
    // The band's top as a multiple of the percent: M2CullFadeBand with m2CullFade on, and 1, no band, with it off.
    float effectiveM2CullFadeBand() const {
        return m2CullFade ? M2CullFadeBand : 1.0f;
    }
    // Which WoWee shader ports draw, as a bitmask: 1 water, 2 terrain, 4 WMO, 8 M2, 16 magma and slime. The rest of each
    // family keeps the retail shading. Reaches the shaders in the scene-wide block's uSceneSize_DisableLightBuffer.w.
    int woweeShaders = 17;
    // A model starts its first animation at a random point of it, so the instances of one model (a herd, a stand of trees)
    // no longer move in step. Read as a model loads.
    bool randomAnimationPhase = false;
    // The shading of WoWee's foliage on the alpha-keyed batches of the models: a hue and brightness shift per instance,
    // crowns darker low and brighter on top, and a warm backlight against the sun. The forward pass shades it all, the
    // G-buffer pass only the shift.
    bool foliageTint = false;
    // How strongly foliageTint shows, from 0 to 2: 0 leaves the models as they are, 1 is the full shading, and 2 doubles
    // every term of it. Reaches the shaders in the scene-wide block's uWoweeLook.z.
    float foliageTintStrength = 1.0f;
    // A model the small-model cull is about to drop fades out over the band above minM2ScreenPercent, to nothing at the
    // percent itself, instead of vanishing. Every batch of the model dissolves, the solid ones through the alpha-keyed
    // pipeline, which can drop fragments.
    bool m2CullFade = false;
    // How far above minM2ScreenPercent the band reaches, as a multiple of the percent.
    static constexpr float M2CullFadeBand = 2;
    // The sky cone takes WoWee's sun glow: a warm scatter and a soft glow around the sun, and a haze at the horizon.
    bool skyGlow = false;
    // Zone light is eased toward its target with a time constant of a fifth of a second, so a jump at a light volume,
    // a zone light or a WMO fog does not show as a pop. Time that does not pass, a paused animation, takes the target.
    bool lightSmoothing = false;
    // Water above the surface mixes in the sky at grazing angles, as WoWee's water does with its planar reflection,
    // here from a gradient of the zone's sky colours. Takes effect where the WoWee water shading draws, woweeShaders
    // bit 1, and not on water inside a WMO.
    bool waterSkyReflection = false;
    // M2 and WMO batches draw their back faces too, where the game culls them, so geometry whose hidden sides were never
    // modelled stops showing through from the air. A back face is lit as its own side. Terrain, liquids, particles, ribbons
    // and the sky keep their culling. A material builds its pipeline once, so a change takes effect from the next flight.
    bool drawBackFaces = true;
    // Particles of weather around the camera, as WoWee draws them: 0 clear, 1 rain, 2 snow, 3 storm, which is rain with
    // lightning. They fall after the opaque pass and are depth-tested, and none fall while the camera is inside a WMO
    // interior. Any other value counts as clear. A change shows from the next frame.
    int weatherType = 0;
    // How much of it falls, from 0 to 1: the share of the particles WoWee's weather draws at most, and in a storm how
    // often lightning strikes.
    float weatherIntensity = 0.5f;
    // The road recorder's ghost line, drawn in the weather pass: the baked roads near the camera and the running take. Empty draws nothing.
    std::shared_ptr<RoadLines> roadLines = std::make_shared<RoadLines>();
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
