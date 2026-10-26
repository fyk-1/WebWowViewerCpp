//
// Created by deamon on 15.06.17.
//

#ifndef WOWVIEWERLIB_CONFIG_H
#define WOWVIEWERLIB_CONFIG_H

#include <algorithm>
#include <compare>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <mathfu/glsl_mappings.h>
#include "../renderer/mapScene/FrameDependentData.h"
#include "../renderer/mapScene/roadLines/RoadLines.h"

class BlpTexture;

// The picture of the weather's particles, loaded by the host mode and read by the renderer on its own thread.
class WeatherPicture {
public:
    void set(std::shared_ptr<BlpTexture> picture) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_picture = std::move(picture);
    }
    std::shared_ptr<BlpTexture> get() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_picture;
    }
private:
    mutable std::mutex m_mutex;
    std::shared_ptr<BlpTexture> m_picture;
};

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

// One entry of an IdSet: a FileDataID, and the index of one of the WMO's groups where the entry names that group alone, else -1.
// Its text form is `<id>` or `<id>:<group>`.
struct IdEntry {
    int fileId = 0;
    int group = -1;
    auto operator<=>(const IdEntry &) const = default;

    std::string text() const { return group < 0 ? std::to_string(fileId) : std::to_string(fileId) + ":" + std::to_string(group); }
    // Reads the text form; false for anything else.
    static bool parse(const std::string &text, IdEntry &entry);
};

inline bool IdEntry::parse(const std::string &text, IdEntry &entry) {
    const auto colon = text.find(':');
    try {
        size_t used = 0;
        const std::string idText = text.substr(0, colon);
        const long long id = std::stoll(idText, &used);
        if (used != idText.size() || id < (std::numeric_limits<int>::min)() || id > (std::numeric_limits<int>::max)()) return false;
        entry.fileId = (int) id;
        entry.group = -1;
        if (colon == std::string::npos) return true;
        const std::string groupText = text.substr(colon + 1);
        const long long group = std::stoll(groupText, &used);
        if (used != groupText.size() || group < 0 || group > (std::numeric_limits<int>::max)()) return false;
        entry.group = (int) group;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// A set of ids the culling threads read while the command thread replaces it, so both go through lock. An entry names a model or a
// WMO whole, in `ids`, or names some groups of a WMO alone, in `groups`; a field whose reader looks at `ids` only ignores the latter.
struct IdSet {
    std::unordered_set<int> ids;
    std::unordered_map<int, std::unordered_set<int>> groups;
    mutable std::shared_mutex lock;

    // Whether the WMO's group is named, by the WMO whole or by that group. The caller holds lock.
    bool namesGroup(int fileId, int group) const {
        if (ids.count(fileId) > 0) return true;
        const auto it = groups.find(fileId);
        return it != groups.end() && it->second.count(group) > 0;
    }
    // Every entry, sorted. The caller holds lock.
    std::vector<IdEntry> entries() const {
        std::vector<IdEntry> result;
        for (int id : ids) result.push_back({id, -1});
        for (const auto &[id, groupIds] : groups) for (int group : groupIds) result.push_back({id, group});
        std::sort(result.begin(), result.end());
        return result;
    }
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
    // Draws the land beyond the draw distance out to the continent's edge, from the map's WDL heights wearing its minimap tiles.
    bool farTerrain = false;
    // The fog toward the horizon, from the camera to the continent's edge: 0 none, 1 full at the edge, 2 full at half the way.
    float farTerrainFog = 1;
    // The share of the full scene that shows against the far land, from 1 (all of it) to 0 (none): below 1 the tiles, models and
    // liquids dissolve into the far land in the screen-door pattern of m2FadeStartPercent. Read once per culling pass with farTerrainOnly.
    // Only the far terrain's own draw reads it, so it does nothing while farTerrain is off.
    float farTerrainShare = 1;
    // No tile, model or spawn is requested or drawn, and at a share of 0 the full scene's draw is skipped as well. Whoever sets it
    // fades the share to 0 first. Read once per culling pass with farTerrainShare, and only while farTerrain is on.
    bool farTerrainOnly = false;
    // The folder of baked height tiles (`<build>/<map id>/<row>_<col>.h16`, FarHeights) the far land displaces its tiles by, empty for none.
    // Set once from the `--heights` launch flag and not a settable field.
    std::string farHeightsDir;
    // Draws a far tile that has a baked height file at the file's heights, on the shared grid; off draws every tile at the WDL's.
    bool farTerrainHeights = false;
    // The yards from the camera to a baked far tile's box from which it draws with half as many cells a side (32 x 32), and with a
    // quarter (16 x 16).
    float farGridHalfDistance = 1500.0f;
    float farGridQuarterDistance = 6000.0f;
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
    // while that event's id is in it. Empty draws the spawns no event ties and the ones that are gone only while an event runs.
    IdSet activeSpawnEvents;
    // The FileDataIDs of the M2 models and the WMOs the cull drops wherever they stand, as it drops one outside the view: what a height
    // bake leaves out of the heights. A WMO entry naming a group drops that group alone. Only the bake sets it, and empty drops none.
    IdSet modelBlock;
    // The FileDataIDs of the M2 models and the WMOs a height bake records beside its tiles (FarHeights::writeLandmarks), for the far
    // pass to draw as themselves at any distance; a WMO entry naming a group draws that group alone. Only the bake reads it.
    IdSet landmarks;
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
    // The forward ADT, WMO and M2 shaders output their material colour with no lighting, for the bake.
    bool bakeUnlit = false;


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
    // Yards ahead on a camera's route whose map tiles are loaded before they are seen. 0 or less turns it off.
    // Only a camera that knows its route answers it (the flyover path camera).
    float preloadAhead = 1200;
    // Frames per second the main loop holds to at most, while vsyncDivisor is 0. 0 or less leaves it uncapped.
    float frameRateCap = 0;
    // 1 to 4 starts each frame on every Nth vertical blank of the window's monitor (Windows only) and ignores frameRateCap;
    // 0 leaves the pace to frameRateCap. Other values clamp to 0..4.
    int vsyncDivisor = 0;
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
    // drop out at a distance and large ones stay. Doodads inside WMOs are not affected. 0 or less culls nothing.
    float minM2ScreenPercent = 1;
    // Skips a model whose world box has no side longer than this, in yards, whatever its distance, in the same cull as
    // minM2ScreenPercent and whatever that is. 0 or less skips none.
    float minM2WorldSize = 0;
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
    // A model the small-model cull is about to drop fades out from this percent of the view's height down to
    // minM2ScreenPercent, where it is gone, instead of vanishing. At or below minM2ScreenPercent nothing fades. Every batch
    // of the model dissolves, the solid ones through the alpha-keyed pipeline, which can drop fragments.
    float m2FadeStartPercent = 0;
    bool m2CullFading() const { return minM2ScreenPercent > 0 && m2FadeStartPercent > minM2ScreenPercent; }
    // The sky cone takes WoWee's sun glow: a warm scatter and a soft glow around the sun, and a haze at the horizon.
    bool skyGlow = false;
    // Zone light is eased toward its target with a time constant of a fifth of a second, so a jump at a light volume,
    // a zone light or a WMO fog does not show as a pop. Time that does not pass, a paused animation, takes the target.
    bool lightSmoothing = false;
    // Each map tile takes the exterior and water light a camera standing at its centre would get, blended between tile centres by world position.
    bool tileLight = false;
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
    // The FileDataID of the picture the particles are drawn with, 0 for the viewer's own dot and streak. The picture is loaded
    // from the install when this changes, and shows once it has loaded. A change shows from the next frame.
    int weatherTexture = 0;
    // The picture weatherTexture names, loaded by the host mode and held here for the renderer. Empty while it is 0.
    std::shared_ptr<WeatherPicture> weatherPicture = std::make_shared<WeatherPicture>();
    // The road recorder's ghost line, drawn in the weather pass: the baked roads near the camera and the running take. Empty draws nothing.
    std::shared_ptr<RoadLines> roadLines = std::make_shared<RoadLines>();
    // Set by the host when the camera jumps, so the next FSR dispatch drops its history. The renderer clears it.
    bool fsrResetRequested = false;
    float fogDensityIncreaser = 0;
    // Set when the database opens, from IClientDatabase::usesClassicFog: the map renderer then draws fog that rises from the ground,
    // classicFogGroundZ, densest there. The ground density is the game's: 90 % cover at the zone's FogEnd. It thins exponentially with
    // the camera's height (classicFogKept at classicFogKeptAt) over that ground or over the fragment, whichever lies lower, and the
    // fragment takes the density at that height along its whole distance.
    bool classicFog = false;
    // The share of the ground fog's density kept at classicFogKeptAt yards over the ground, in percent, 1 to 99; 20 keeps 4 % at twice that
    // height.
    float classicFogKept = 20;
    // The height over the ground, in yards, at which classicFogKept percent of the ground density is left.
    float classicFogKeptAt = 300;
    // The world height the classic fog rises from, in z, which a script sets from the smoothed terrain under the camera; -99999, the
    // engine's own "no height", is unset, and the viewer then takes the terrain under the camera as it is. The fog is densest there and
    // thins with the camera's height above it. It is no NaN, which the host's JSON would read as no field and refuse a script's set of it.
    float classicFogGroundZ = -99999.0f;

    // The final composite adds no glow, so the frame is the scene's colours alone (the height bake sets it).
    bool disableGlow = false;

    bool pauseAnimation = false;

    // When true (and supported by the device/renderer), M2 skeletal animation,
    // particle emitters and ribbon emitters are evaluated on GPU (Vulkan compute)
    // instead of CPU. Per-object fallback to CPU happens automatically.
    bool useGpuAnimation = true;

    mathfu::vec4 clearColor = {0.117647, 0.207843, 0.392157, 0};

    EParameterSource globalLighting = EParameterSource::eDatabase;
    ExteriorColors exteriorColors;
    mathfu::vec3 exteriorDirectColorDir = {0, 0, 0};

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

#endif //WOWVIEWERLIB_CONFIG_H
