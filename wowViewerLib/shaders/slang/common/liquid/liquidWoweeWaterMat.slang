#ifndef LIQUID_WOWEE_WATER_MATERIAL_SLANG
#define LIQUID_WOWEE_WATER_MATERIAL_SLANG

import glsl;

#include "../commonLiquidStructs.slang"
#include "./commonLiquidFunctions.slang"
#include "../commonLightFunctions.slang"
#include "../commonFogFunctions.slang"
#include "../commonGBufferData.slang"

// robots@Impl: A port of WoWee's water shader (assets/shaders/water.frag.glsl), in place of the retail-derived
// calcWaterLiquidMat, which renders Era water as a flat opaque sheet. Kept: procedural wave normals, screen-space
// refraction of the liquid backbuffer, Beer-Lambert absorption, the shallow-to-deep body colour, shoreline wet band
// and foam, GGX sun specular with sparkle, the alpha model and the horizon fade. Dropped, for lack of the inputs:
// planar reflection, wake froth, wave crests and subsurface scattering (WoWee feeds those from its own vertex
// displacement and player state). The body colours and alphas are WoWee's per-type constants. Scene time arrives in
// milliseconds; WoWee's constants are tuned in seconds.

float ww_hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float ww_hash22x(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453);
}

float ww_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = ww_hash21(i);
    float b = ww_hash21(i + vec2(1.0, 0.0));
    float c = ww_hash21(i + vec2(0.0, 1.0));
    float d = ww_hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float ww_fbm(vec2 p, float time) {
    float v = 0.0;
    v += ww_noise(p * 3.0 + time * 0.3) * 0.5;
    v += ww_noise(p * 6.0 - time * 0.5) * 0.25;
    v += ww_noise(p * 12.0 + time * 0.7) * 0.125;
    return v;
}

float ww_cellular(vec2 p, float jitter) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 cellId = i + neighbor;
            vec2 point = vec2(ww_hash21(cellId), ww_hash22x(cellId)) * jitter + vec2(0.5) * (1.0 - jitter);
            minDist = min(minDist, length(neighbor + point - f));
        }
    }
    return minDist;
}

vec3 ww_waveNormal(vec2 p, float time) {
    vec2 d1 = normalize(vec2(0.86, 0.51));
    vec2 d2 = normalize(vec2(-0.47, 0.88));
    vec2 d3 = normalize(vec2(0.32, -0.95));
    float f1 = 0.19, f2 = 0.43, f3 = 0.72;
    float s1 = 0.95, s2 = 1.73, s3 = 2.40;
    float a1 = 0.22, a2 = 0.10, a3 = 0.05;

    vec2 p1 = p + d1 * (time * s1 * 4.0);
    vec2 p2 = p + d2 * (time * s2 * 4.0);
    vec2 p3 = p + d3 * (time * s3 * 4.0);

    float warp = ww_noise(p * 0.018 + vec2(time * 0.015)) * 6.28318;
    float c1 = cos(dot(p1, d1) * f1 + warp);
    float c2 = cos(dot(p2, d2) * f2 + warp * 1.7);
    float c3 = cos(dot(p3, d3) * f3 + warp * 2.6);

    float dHx = c1 * d1.x * f1 * a1 + c2 * d2.x * f2 * a2 + c3 * d3.x * f3 * a3;
    float dHy = c1 * d1.y * f1 * a1 + c2 * d2.y * f2 * a2 + c3 * d3.y * f3 * a3;
    return normalize(vec3(-dHx, -dHy, 1.0));
}

float ww_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-7);
}

float ww_smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

// View depth of the opaque scene at a screen position; the sky, which the GBuffer never writes, reads as very far.
float ww_sceneDepth(vec2 uv, in SceneWideParams scene) {
    if (texture(depthTex, uv).r <= 0.0001) return 100000.0;
    return abs(sceneViewDepth(uv * screenSize.xy, scene));
}

vec4 calcWoweeWaterMat(in LiquidInstanceData inst, in WaterData data, in SceneWideParams scene, in PSFog fog,
                       in vec3 worldPos, in vec3 viewPos, in vec2 fragCoord)
{
    float time = mod(scene.uViewUpSceneTime.w, 3600000.0) * 0.001;
    int liquidType = inst.liquidMaterial_liquidType_isInterior_vtxCellOffetWidthHeight.y;
    bool isOcean = data.waterType_pad_pad_pad.x == 0;
    // WMO water and Slow Water get WoWee's canal profile: a deeper body colour with a floor, more shimmer.
    bool canal = liquidType == 5 || liquidType == 13 || liquidType == 17;

    // The body colours are the zone's: LightData's river or ocean colours, near and far, which the renderer blends
    // like every other light value. WoWee's own blues stand in only where the light data gives none.
    vec3 waterColor = isOcean ? scene.closeOceanColor_shallowAlpha.rgb : scene.closeRiverColor_shallowAlpha.rgb;
    vec3 farColor = isOcean ? scene.farOceanColor_deepAlpha.rgb : scene.farRiverColor_deepAlpha.rgb;
    if (dot(waterColor + farColor, vec3(1.0)) < 0.001) {
        waterColor = isOcean ? vec3(0.04, 0.16, 0.38) : vec3(0.10, 0.28, 0.55);
        farColor = waterColor * vec3(0.3, 0.5, 0.7);
    }
    float waterAlpha = isOcean ? 0.72 : 0.48;
    float shimmerStrength = canal ? 0.95 : 0.50;
    float alphaScale = canal ? 1.15 : 1.00;
    float bodyFloor = canal ? 0.45 : 0.0;
    bool terrainWater = !canal;

    vec3 ldir = normalize((scene.uInvLookAtMat * vec4(-scene.extLight.uExteriorDirectColorDir.xyz, 0.0)).xyz);
    vec3 lightColor = scene.extLight.uExteriorDirectColor.rgb;
    vec3 ambientColor = scene.extLight.uExteriorAmbientColor.rgb;

    vec2 screenUV = fragCoord / max(screenSize.xy, vec2(1.0));
    float dist = length(viewPos);

    vec3 detailNorm = ww_waveNormal(worldPos.xy, time);
    float detailFade = 1.0 - smoothstep(250.0, 1400.0, dist);
    vec3 norm = normalize(mix(vec3(0.0, 0.0, 1.0), detailNorm, 0.55 * detailFade));

    // worldPos is placement-local for WMO water, so the direction comes from view space.
    vec3 viewDir = normalize((scene.uInvLookAtMat * vec4(-viewPos, 0.0)).xyz);
    float NdotV = max(dot(norm, viewDir), 0.001);
    float NdotL = max(dot(norm, ldir), 0.0);

    const vec3 F0 = vec3(0.02);
    float fresnel = F0.x + (1.0 - F0.x) * pow(1.0 - NdotV, 5.0);

    // Refraction
    vec2 refractUV = clamp(screenUV + norm.xy * (0.02 + 0.03 * fresnel), vec2(0.001), vec2(0.999));
    vec3 sceneRefract = texture(liquidBackBuffer, refractUV).rgb;

    float waterLinDepth = abs(viewPos.z);
    float depthDiff = max(ww_sceneDepth(refractUV, scene) - waterLinDepth, 0.0);
    float verticalFactor = abs(viewDir.z);
    float verticalDepth = depthDiff * max(verticalFactor, 0.05);
    float shoreDepth = max(ww_sceneDepth(screenUV, scene) - waterLinDepth, 0.0) * max(verticalFactor, 0.05);

    // Absorption and body colour
    // The water absorbs most the channels its colour lacks, so the refracted floor tints toward the body colour; for
    // WoWee's blue this lands close to its own fixed coefficients.
    vec3 tint = waterColor / max(max(waterColor.r, waterColor.g), max(waterColor.b, 0.001));
    vec3 absorptionCoeff = (1.0 - tint) * 0.5 + 0.04;
    vec3 absorbed = exp(-absorptionCoeff * verticalDepth);

    vec3 underwaterFogColor = mix(waterColor, farColor, 0.5);
    float underwaterFogFade = 1.0 - exp(-verticalDepth * 0.35);
    vec3 foggedScene = mix(sceneRefract, underwaterFogColor, underwaterFogFade);

    vec3 shallowColor = waterColor * 1.2;
    vec3 deepColor = farColor;
    float depthFade = max(1.0 - exp(-verticalDepth * 0.15), bodyFloor);
    vec3 waterBody = mix(shallowColor, deepColor, depthFade);

    vec3 refractedColor = mix(foggedScene * absorbed, waterBody, depthFade * 0.7);
    if (verticalDepth < 0.01) {
        float opticalDepth = 1.0 - exp(-dist * 0.004);
        refractedColor = mix(foggedScene, waterBody, opticalDepth * 0.6);
    }
    vec3 litBase = waterBody * (ambientColor * 0.7 + NdotL * lightColor * 0.5);
    refractedColor = mix(refractedColor, litBase, clamp(depthFade * 0.3, 0.0, 0.5));

    // Shoreline wet band
    float swashPhase = sin(time * 0.55 + ww_noise(worldPos.xy * 0.12) * 6.28);
    float swashDepth = 0.30 + 0.18 * swashPhase;
    float wetBand = 1.0 - smoothstep(0.0, 0.70, shoreDepth);
    float shallowBand = 1.0 - smoothstep(0.0, 2.20, shoreDepth);
    float coveredness = smoothstep(swashDepth - 0.12, swashDepth + 0.08, shoreDepth);
    if (shallowBand > 0.001) {
        float sediment = ww_noise(worldPos.xy * 1.6 + time * vec2(0.10, 0.06)) * 0.6
                       + ww_noise(worldPos.xy * 3.4 - time * vec2(0.07, 0.13)) * 0.4;
        float banding = smoothstep(0.42, 0.78, sediment);
        refractedColor *= 1.0 + banding * 0.18 * shallowBand;
        float soak = wetBand * (0.42 + 0.58 * coveredness);
        refractedColor = mix(refractedColor, refractedColor * vec3(0.62, 0.60, 0.56), soak * 0.80);
    }

    // Specular
    float roughness = 0.18;
    vec3 halfDir = normalize(ldir + viewDir);
    float D = ww_ggx(norm, halfDir, roughness);
    float G = ww_smith(NdotV, NdotL, roughness);
    float cosHV = max(dot(halfDir, viewDir), 0.0);
    vec3 F = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosHV, 0.0, 1.0), 5.0);
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001) * lightColor * NdotL;
    specular = min(specular, vec3(2.0));
    if (dist < 700.0) {
        float sparkleNoise = ww_fbm(worldPos.xy * 4.0 + time * 0.5, time * 1.5);
        float sparkle = pow(max(sparkleNoise - 0.55, 0.0) / 0.45, 3.0) * shimmerStrength * 0.10;
        specular += sparkle * lightColor * (1.0 - smoothstep(150.0, 700.0, dist));
    }

    vec3 color = refractedColor + specular;

    // Shoreline foam
    float shorelineFoam = 0.0;
    if (terrainWater && shoreDepth > 0.001 && shoreDepth < 0.9) {
        float foamDepthMask = 1.0 - smoothstep(0.0, 0.9, shoreDepth);

        vec2 drift = vec2(ww_noise(worldPos.xy * 0.09) - 0.5, ww_noise(worldPos.xy * 0.09 + vec2(19.0)) - 0.5);
        drift = normalize(drift + vec2(0.001));
        vec2 swashAdvect = drift * swashPhase * 1.35;

        vec2 warpOffset = vec2(
            ww_noise(worldPos.xy * 2.5 + time * 0.08) - 0.5,
            ww_noise(worldPos.xy * 2.5 + vec2(37.0) + time * 0.06) - 0.5) * 1.6;
        warpOffset += vec2(
            ww_noise(worldPos.xy * 6.1 - time * 0.11) - 0.5,
            ww_noise(worldPos.xy * 6.1 + vec2(11.0) + time * 0.09) - 0.5) * 0.55;
        vec2 foamUV = worldPos.xy + warpOffset + swashAdvect;

        const mat2 rot1 = mat2(0.87, -0.50, 0.50, 0.87);
        const mat2 rot2 = mat2(0.26, -0.97, 0.97, 0.26);
        float thresholdJitter = ww_noise(worldPos.xy * 5.0 + time * 0.2);

        float cells1 = ww_cellular(rot1 * foamUV * 13.0 + time * vec2(0.15, 0.08), 1.0);
        float foam1 = (1.0 - smoothstep(0.0, 0.075 + 0.075 * thresholdJitter, cells1)) * 0.45;
        float cells2 = ww_cellular(rot2 * foamUV * 27.7 + time * vec2(-0.12, 0.22), 1.0);
        float foam2 = (1.0 - smoothstep(0.0, 0.045 + 0.05 * (1.0 - thresholdJitter), cells2)) * 0.30;
        float cells3 = ww_cellular(foamUV * 51.3 + time * vec2(0.25, -0.1), 1.0);
        float foam3 = (1.0 - smoothstep(0.0, 0.05, cells3)) * 0.18;

        float noiseMask = ww_noise(worldPos.xy * 3.0 + time * 0.15);
        float foam = (foam1 + foam2 + foam3) * foamDepthMask * smoothstep(0.3, 0.6, noiseMask);

        float swashBand = 1.0 - smoothstep(0.0, 0.085, abs(shoreDepth - swashDepth));
        float swashTexture = 0.55 + 0.45 * ww_cellular(rot1 * foamUV * 8.6 + time * vec2(0.05, 0.12), 1.0);
        foam += swashBand * swashTexture * 0.55 * foamDepthMask;

        float sprayMask = 1.0 - smoothstep(0.0, 0.11, abs(shoreDepth - swashDepth + 0.04));
        float sprayCells = ww_cellular(rot2 * (foamUV + swashAdvect * 0.5) * 96.0 + time * vec2(0.6, -0.45), 1.0);
        float spray = (1.0 - smoothstep(0.0, 0.035, sprayCells)) * sprayMask * max(swashPhase, 0.0) * 0.7;
        foam += spray * foamDepthMask;

        foam *= smoothstep(0.0, 0.025, shoreDepth);
        foam = clamp(foam, 0.0, 0.85);
        color = mix(color, vec3(0.78, 0.85, 0.92), foam * 0.48);
        shorelineFoam = foam;
    }

    // Alpha
    float baseAlpha = mix(waterAlpha, min(1.0, waterAlpha * 1.5), depthFade);
    float alpha = mix(baseAlpha, min(1.0, baseAlpha * 1.3), fresnel) * alphaScale;
    alpha = clamp(alpha, 0.15, 0.92);
    alpha = max(alpha, wetBand * 0.42 * smoothstep(0.0, 0.05, shoreDepth));
    alpha = max(alpha, shorelineFoam * 0.58);
    alpha *= smoothstep(2400.0, 600.0, dist);

    vec4 fogged = makeFog2(fog, vec4(color, alpha), scene.uViewUpSceneTime.xyz, viewPos,
                           scene.extLight.uExteriorDirectColorDir.xyz, 2);
    return vec4(fogged.rgb, alpha);
}

#endif //LIQUID_WOWEE_WATER_MATERIAL_SLANG
