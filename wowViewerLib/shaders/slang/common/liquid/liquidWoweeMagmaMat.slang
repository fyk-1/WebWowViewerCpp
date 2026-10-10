#ifndef LIQUID_WOWEE_MAGMA_MATERIAL_SLANG
#define LIQUID_WOWEE_MAGMA_MATERIAL_SLANG

import glsl;

#include "../commonLiquidStructs.slang"
#include "../commonLightFunctions.slang"
#include "../commonFogFunctions.slang"

// robots@Impl: A port of the magma and slime branch of WoWee's water shader (assets/shaders/water.frag.glsl), in place
// of calcMagmaLiquidMat, which blends the retail texture layers. Kept: the gradient-noise flow field, the crust to
// molten to core palette, the pulse and the hot-core brightening. Dropped: the retail layers and DB colour, which
// WoWee has no counterpart for; its palettes are constants per type. Slime is told from magma by LiquidType id, and
// Green Lava takes the slime palette. The viewer's fog replaces WoWee's. Scene time arrives in milliseconds; WoWee's
// constants are tuned in seconds.

vec2 wg_gradHash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float wg_gradNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = dot(wg_gradHash(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0));
    float b = dot(wg_gradHash(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
    float c = dot(wg_gradHash(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
    float d = dot(wg_gradHash(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.5 + 0.5;
}

// Rotating every octave keeps the lattices from lining up, so the flow reads as a fractal and not a grid of squares.
float wg_fbmGrad(vec2 p, float time) {
    const mat2 R = mat2(0.80, -0.60, 0.60, 0.80);
    float v = 0.0;
    v += wg_gradNoise(p * 3.0 + time * 0.3) * 0.5;
    p = R * p;
    v += wg_gradNoise(p * 6.0 - time * 0.5) * 0.25;
    p = R * p;
    v += wg_gradNoise(p * 12.0 + time * 0.7) * 0.125;
    return v;
}

vec4 calcWoweeMagmaMat(in LiquidInstanceData inst, in SceneWideParams scene, in PSFog fog,
                       in vec3 worldPos, in vec3 viewPos)
{
    float time = mod(scene.uViewUpSceneTime.w, 3600000.0) * 0.001;
    int liquidType = inst.liquidMaterial_liquidType_isInterior_vtxCellOffetWidthHeight.y;
    // WoWee's basic-type rule, with Green Lava (15) as the one exception.
    bool isSlime = (liquidType > 0 && (liquidType - 1) % 4 == 3) || liquidType == 15;

    vec2 flowUV = worldPos.xy;
    float n1 = wg_fbmGrad(flowUV * 0.06 + vec2(time * 0.02, time * 0.03), time * 0.4);
    float n2 = wg_fbmGrad(flowUV * 0.10 + vec2(-time * 0.015, time * 0.025), time * 0.3);
    float n3 = wg_gradNoise(flowUV * 0.25 + vec2(time * 0.04, -time * 0.02));
    float flow = n1 * 0.45 + n2 * 0.35 + n3 * 0.20;

    vec3 crustColor = isSlime ? vec3(0.05, 0.15, 0.02) : vec3(0.15, 0.04, 0.01);
    vec3 hotColor = isSlime ? vec3(0.3, 0.8, 0.15) : vec3(1.0, 0.45, 0.05);
    vec3 coreColor = isSlime ? vec3(0.5, 1.0, 0.3) : vec3(1.0, 0.85, 0.3);

    float crustMask = smoothstep(0.25, 0.50, flow);
    float coreMask = smoothstep(0.60, 0.80, flow);
    vec3 color = mix(crustColor, hotColor, crustMask);
    color = mix(color, coreColor, coreMask);

    color *= 1.0 + 0.15 * sin(time * 1.5 + flow * 6.0);
    color *= 1.0 + coreMask * 0.6;

    vec4 fogged = makeFog2(fog, vec4(color, 0.97), scene.uViewUpSceneTime.xyz, viewPos,
                           scene.extLight.uExteriorDirectColorDir.xyz, 2);
    return vec4(fogged.rgb, 0.97);
}

#endif //LIQUID_WOWEE_MAGMA_MATERIAL_SLANG
