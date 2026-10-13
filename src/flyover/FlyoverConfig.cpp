//
// Flyover: the render Config exposed by field name, for the stdin `config` command and the route file.
//

#include "FlyoverConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include "../../3rdparty/nlohmann/json.hpp"

/*
robots@Impl: The name table.

FLYOVER_CONFIG_FIELDS is the one list of settable fields. Each entry is a kind and a member path from Config,
and the name is that path as written, so `exteriorColors.exteriorAmbientColor` is the name of the member of
that path. A field is added or removed here and nowhere else. An IntSet field is set from comma-separated integers, or from
`none` for the empty set, and dumped as a sorted array.

The debug toggles that break the frame are left out on purpose: stopBufferUpdates, stepBufferUpdate,
doubleCameraDebug, controlSecondCamera, swapMainAndDebug, cameraM2 and drawDepthBuffer. The structs
liquidColors, skyColors and fogResult, the sky-scene condition set and the river override holder are not
in the table either, nor is fsrResetRequested, a request that FlyoverMode sets and the renderer clears.
*/

namespace {
    enum class Kind { Bool, Int, U16, Float, Double, Vec3, Vec4, IntSet, ParamSource, FreeStrategy };

    struct Field {
        const char *name;
        Kind kind;
        void *(*pointer)(Config &);
    };

#define FLYOVER_CONFIG_FIELDS(X) \
    X(Bool, renderAdt) \
    X(Bool, farTerrain)     X(Float, farTerrainFog)     X(Float, farTerrainSwapHeight) \
    X(Bool, renderWMO) \
    X(Bool, skipUnreachableWmoGroups) \
    X(Bool, renderM2) \
    X(Bool, renderM2Decals) \
    X(Bool, renderRibbons) \
    X(Bool, renderSkyDom) \
    X(Bool, renderSkyScene) \
    X(Bool, renderLiquid) \
    X(Bool, renderCreatures) \
    X(Bool, renderHumanoids) \
    X(Bool, renderGameObjectSpawns) \
    X(IntSet, activeSpawnEvents) \
    X(Bool, renderBSP) \
    X(Bool, renderPortals) \
    X(Bool, renderAntiPortals) \
    X(Bool, renderPortalsIgnoreDepth) \
    X(Bool, usePortalCulling) \
    X(Bool, discardInvisibleMeshes) \
    X(Bool, ignoreADTHoles) \
    X(Bool, ignoreADTColoring) \
    X(Bool, ignoreWMOColoring) \
    X(Bool, renderAllWmoDoodads) \
    X(Bool, disableFog) \
    X(Bool, drawDebugLights) \
    X(Bool, drawWmoBB) \
    X(Bool, drawM2BB) \
    X(Bool, enableObjectPicking) \
    X(Bool, enableFXAA) \
    X(Bool, fsrUpscaling) \
    X(Bool, enableLightBuffer) \
\
    X(Bool, disableGlow) \
    X(Bool, pauseAnimation) \
    X(Bool, useGpuAnimation) \
    X(Bool, useMinimapWaterColor) \
    X(Bool, useCloseRiverColorForDB) \
    X(Int, wmoMinBatch) \
    X(Int, wmoMaxBatch) \
    X(Int, m2MinBatch) \
    X(Int, m2MaxBatch) \
    X(Int, minParticle) \
    X(Int, maxParticle) \
    X(Int, diffuseColorHack) \
    X(U16, currentTime) \
    X(Float, timeMultiplier) \
    X(Float, fov) \
    X(Float, nearPlane) \
    X(Float, farPlane) \
    X(Float, farPlaneForCulling)     X(Float, farPlaneForCullingAir) \
    X(Float, preloadAhead) \
    X(Float, frameRateCap) \
    X(Float, renderScale) \
    X(Float, fsrSharpness)     X(Float, fsrOutputScale)     X(Float, fsrMaxHeight)     X(Float, minM2ScreenPercent) \
    X(Float, groundAltitude)   X(Float, minM2ScreenPercentAir) \
    X(Int, woweeShaders)     X(Bool, randomAnimationPhase)   X(Bool, foliageTint)   X(Float, foliageTintStrength)   X(Bool, m2CullFade) \
    X(Bool, skyGlow)   X(Bool, lightSmoothing)   X(Bool, waterSkyReflection)   X(Bool, drawBackFaces)     X(Int, weatherType)   X(Float, weatherIntensity) \
    X(Float, fogDensityIncreaser)     X(Float, classicFogStrength) X(Float, classicFogFalloff) \
    X(Float, adtSpecMult) \
    X(Float, currentGlow) \
    X(Double, adtTTLWithoutUpdate) \
    X(Double, adtFTLWithoutUpdate) \
    X(Vec4, clearColor) \
    X(Vec3, exteriorDirectColorDir) \
    X(Vec3, exteriorColors.exteriorAmbientColor) \
    X(Vec3, exteriorColors.exteriorHorizontAmbientColor) \
    X(Vec3, exteriorColors.exteriorGroundAmbientColor) \
    X(Vec3, exteriorColors.exteriorDirectColor) \
    X(Vec3, exteriorColors.exteriorSpecularColor) \
    X(ParamSource, globalLighting) \
    X(ParamSource, waterColorParams) \
    X(ParamSource, skyParams) \
    X(ParamSource, globalFog) \
    X(ParamSource, glowSource) \
    X(FreeStrategy, adtFreeStrategy)

#define FLYOVER_FIELD(kind, member) \
    {#member, Kind::kind, [](Config &config) -> void * { return &config.member; }},

    const Field kFields[] = {
        FLYOVER_CONFIG_FIELDS(FLYOVER_FIELD)
    };

#undef FLYOVER_FIELD

    struct EnumName {
        const char *name;
        int value;
    };

    const EnumName kParamSources[] = {
        {"eNone", (int) EParameterSource::eNone},
        {"eConfig", (int) EParameterSource::eConfig},
        {"eDatabase", (int) EParameterSource::eDatabase},
        {"eM2", (int) EParameterSource::eM2},
    };

    const EnumName kFreeStrategies[] = {
        {"eTimeBased", (int) EFreeStrategy::eTimeBased},
        {"eFrameBase", (int) EFreeStrategy::eFrameBase},
    };

    bool equalsNoCase(const std::string &a, const char *b) {
        size_t i = 0;
        for (; i < a.size() && b[i]; i++) {
            if (std::tolower((unsigned char) a[i]) != std::tolower((unsigned char) b[i])) return false;
        }
        return i == a.size() && b[i] == 0;
    }

    std::string trim(const std::string &s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    bool parseDouble(const std::string &text, double &out) {
        std::string t = trim(text);
        if (t.empty()) return false;

        char *end = nullptr;
        out = std::strtod(t.c_str(), &end);
        return end == t.c_str() + t.size() && std::isfinite(out);
    }

    bool parseLong(const std::string &text, long long &out) {
        std::string t = trim(text);
        if (t.empty()) return false;

        char *end = nullptr;
        out = std::strtoll(t.c_str(), &end, 10);
        return end == t.c_str() + t.size();
    }

    // Comma-separated floats into exactly count slots.
    bool parseFloats(const std::string &text, float *out, int count) {
        std::stringstream stream(text);
        std::string part;
        int i = 0;
        while (std::getline(stream, part, ',')) {
            double value;
            if (i >= count || !parseDouble(part, value)) return false;
            out[i++] = (float) value;
        }
        return i == count;
    }

    bool parseEnum(const std::string &text, const EnumName *names, size_t count, int &out) {
        std::string t = trim(text);
        for (size_t i = 0; i < count; i++) {
            if (equalsNoCase(t, names[i].name)) {
                out = names[i].value;
                return true;
            }
        }
        return false;
    }

    // A float as its shortest text, parsed back as a double, so 0.117647f prints as 0.117647.
    double shortFloat(float value) {
        char buffer[32];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::strtod(std::string(buffer, result.ptr).c_str(), nullptr);
    }

    const char *enumName(const EnumName *names, size_t count, int value) {
        for (size_t i = 0; i < count; i++) {
            if (names[i].value == value) return names[i].name;
        }
        return "unknown";
    }
}

namespace FlyoverConfig {
    std::string set(Config &config, const std::string &name, const std::string &value) {
        const Field *field = nullptr;
        for (const Field &candidate : kFields) {
            if (name == candidate.name) {
                field = &candidate;
                break;
            }
        }
        if (!field) return "unknown name";

        void *target = field->pointer(config);
        std::string text = trim(value);

        switch (field->kind) {
            case Kind::Bool: {
                if (equalsNoCase(text, "true") || text == "1") {
                    *(bool *) target = true;
                } else if (equalsNoCase(text, "false") || text == "0") {
                    *(bool *) target = false;
                } else {
                    return "expected true or false";
                }
                return "";
            }
            case Kind::Int: {
                long long number;
                if (!parseLong(text, number)) return "expected an integer";
                if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
                    return "out of range for an int";
                }
                *(int *) target = (int) number;
                return "";
            }
            case Kind::U16: {
                long long number;
                if (!parseLong(text, number)) return "expected an integer";
                if (number < 0 || number > 65535) return "out of range 0 to 65535";
                *(uint16_t *) target = (uint16_t) number;
                return "";
            }
            case Kind::Float: {
                double number;
                if (!parseDouble(text, number)) return "expected a number";
                if (std::fabs(number) > std::numeric_limits<float>::max()) return "out of range for a float";
                *(float *) target = (float) number;
                return "";
            }
            case Kind::Double: {
                double number;
                if (!parseDouble(text, number)) return "expected a number";
                *(double *) target = number;
                return "";
            }
            case Kind::Vec3: {
                float parts[3];
                if (!parseFloats(text, parts, 3)) return "expected three comma-separated numbers";
                *(mathfu::vec3 *) target = mathfu::vec3(parts[0], parts[1], parts[2]);
                return "";
            }
            case Kind::Vec4: {
                float parts[4];
                if (!parseFloats(text, parts, 4)) return "expected four comma-separated numbers";
                *(mathfu::vec4 *) target = mathfu::vec4(parts[0], parts[1], parts[2], parts[3]);
                return "";
            }
            case Kind::IntSet: {
                // Comma-separated integers, or none for the empty set: a stdin config command carries no empty value.
                std::unordered_set<int> ids;
                if (!equalsNoCase(text, "none")) {
                    std::stringstream stream(text);
                    std::string part;
                    while (std::getline(stream, part, ',')) {
                        long long number;
                        if (!parseLong(part, number) || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
                            return "expected comma-separated integers, or none";
                        }
                        ids.insert((int) number);
                    }
                    if (ids.empty()) return "expected comma-separated integers, or none";
                }
                std::unique_lock lock(config.activeSpawnEventsLock);
                config.activeSpawnEvents = std::move(ids);
                return "";
            }
            case Kind::ParamSource: {
                int enumValue;
                if (!parseEnum(text, kParamSources, std::size(kParamSources), enumValue)) {
                    return "expected eNone, eConfig, eDatabase or eM2";
                }
                *(EParameterSource *) target = (EParameterSource) enumValue;
                return "";
            }
            case Kind::FreeStrategy: {
                int enumValue;
                if (!parseEnum(text, kFreeStrategies, std::size(kFreeStrategies), enumValue)) {
                    return "expected eTimeBased or eFrameBase";
                }
                *(EFreeStrategy *) target = (EFreeStrategy) enumValue;
                return "";
            }
        }
        return "unsupported field";
    }

    std::string dump(const Config &config) {
        nlohmann::ordered_json result = nlohmann::ordered_json::object();

        for (const Field &field : kFields) {
            // Read only: the pointer function takes a mutable Config for the setter's sake.
            void *source = field.pointer(const_cast<Config &>(config));

            switch (field.kind) {
                case Kind::Bool:
                    result[field.name] = *(bool *) source;
                    break;
                case Kind::Int:
                    result[field.name] = *(int *) source;
                    break;
                case Kind::U16:
                    result[field.name] = (int) *(uint16_t *) source;
                    break;
                case Kind::Float:
                    result[field.name] = shortFloat(*(float *) source);
                    break;
                case Kind::Double:
                    result[field.name] = *(double *) source;
                    break;
                case Kind::Vec3: {
                    auto &v = *(mathfu::vec3 *) source;
                    result[field.name] = {shortFloat(v.x), shortFloat(v.y), shortFloat(v.z)};
                    break;
                }
                case Kind::Vec4: {
                    auto &v = *(mathfu::vec4 *) source;
                    result[field.name] = {shortFloat(v.x), shortFloat(v.y), shortFloat(v.z), shortFloat(v.w)};
                    break;
                }
                case Kind::IntSet: {
                    std::shared_lock lock(config.activeSpawnEventsLock);
                    std::vector<int> sorted(config.activeSpawnEvents.begin(), config.activeSpawnEvents.end());
                    std::sort(sorted.begin(), sorted.end());
                    result[field.name] = sorted;
                    break;
                }
                case Kind::ParamSource:
                    result[field.name] = enumName(kParamSources, std::size(kParamSources),
                                                  (int) *(EParameterSource *) source);
                    break;
                case Kind::FreeStrategy:
                    result[field.name] = enumName(kFreeStrategies, std::size(kFreeStrategies),
                                                  (int) *(EFreeStrategy *) source);
                    break;
            }
        }

        return result.dump();
    }
}
