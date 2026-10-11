#ifndef COMMON_WOWEE_SKY_SLANG
#define COMMON_WOWEE_SKY_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: Ports of WoWee's sky shading (assets/shaders/skybox.frag.glsl), under the licence in LICENSE-WoWee.txt
// beside this file. Config::skyGlow and Config::waterSkyReflection reach the shaders in the scene-wide block's
// uWoweeLook, x and y. The glow is WoWee's three scattering terms, whole: a warm Rayleigh scatter near the horizon, the
// sharp and the soft Mie glow around the sun, and the horizon haze. They go on the retail cone colour, which already
// holds the gradient, so the haze takes the cone's own colour where WoWee takes its horizon colour. Left out: the
// moonlight tint, which WoWee adds at night. The gradient is WoWee's three bands, from the zone's sky colours: top over
// middle, middle over the first band, the first over the second where WoWee has its fog colour. Directions are in view
// space, and up is the scene's view-space up vector.

bool woweeSkyGlowOn(in SceneWideParams scene) {
    return scene.uWoweeLook.x > 0.5;
}

bool woweeWaterSkyOn(in SceneWideParams scene) {
    return scene.uWoweeLook.y > 0.5;
}

// dir and sunDir are unit vectors toward the fragment's sky and toward the sun.
vec3 wsky_glow(in vec3 cone, in vec3 dir, in vec3 sunDir, in vec3 up) {
    vec3 sky = cone;
    float elevClamped = clamp(dot(dir, up), 0.0, 1.0);
    float sunDot = max(dot(dir, sunDir), 0.0);
    float sunAboveHorizon = clamp(dot(sunDir, up), 0.0, 1.0);

    float rayleighStrength = pow(1.0 - elevClamped, 3.0) * 0.15;
    vec3 scatterColor = mix(vec3(0.8, 0.45, 0.15), vec3(0.3, 0.5, 1.0), elevClamped);
    sky += scatterColor * rayleighStrength * sunDot * sunAboveHorizon;

    float mieSharp = pow(sunDot, 64.0) * 0.4;
    float mieSoft = pow(sunDot, 8.0) * 0.1;
    vec3 sunGlowColor = mix(vec3(1.0, 0.85, 0.55), vec3(1.0, 1.0, 0.95), elevClamped);
    sky += sunGlowColor * (mieSharp + mieSoft) * sunAboveHorizon;

    float hazeDensity = exp(-elevClamped * 12.0) * 0.06;
    sky += cone * hazeDensity * sunAboveHorizon;
    return sky;
}

// The sky's colour at an elevation, 0 at the horizon and 1 at the zenith.
vec3 wsky_gradient(in float elevation, in SceneWideParams scene) {
    float e = clamp(elevation, 0.0, 1.0);
    if (e > 0.4) {
        return mix(scene.uSkyMiddleColor.rgb, scene.uSkyTopColor.rgb, (e - 0.4) / 0.6);
    }
    if (e > 0.05) {
        return mix(scene.uSkyBand1Color.rgb, scene.uSkyMiddleColor.rgb, (e - 0.05) / 0.35);
    }
    return mix(scene.uSkyBand2Color.rgb, scene.uSkyBand1Color.rgb, e / 0.05);
}

#endif //COMMON_WOWEE_SKY_SLANG
