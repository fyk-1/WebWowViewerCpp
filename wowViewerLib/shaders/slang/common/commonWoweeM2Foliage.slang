#ifndef COMMON_WOWEE_M2_FOLIAGE_SLANG
#define COMMON_WOWEE_M2_FOLIAGE_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: A port of the foliage branches of WoWee's M2 shader (assets/shaders/m2.frag.glsl), under the licence in
// LICENSE-WoWee.txt beside this file. Config::foliageTint reaches the shader as the model's height in the model-wide
// block, 0 with it off, and the alpha-keyed batches (blend mode 1) of such a model count as foliage, since the viewer
// has no foliage flag. Taken whole: the hash over the instance origin that shifts red, blue and brightness, the sky
// bounce on the lit colour by how far the normal points up, the occlusion that darkens a crown toward its foot, and the
// warm backlight a leaf throws toward a camera looking into the sun. Left out: the leaf's own texture alpha as the
// backlight's weight, which the combiner does not hand out, and the wobble of the normal by texture coordinate. The sky
// bounce multiplies the whole lit colour, where WoWee's multiplies the ambient term.

// The shift of the diffuse colour, the same for every fragment of an instance.
vec3 wf_tint(in vec2 instanceOrigin) {
    float hash = fract(sin(dot(instanceOrigin, vec2(127.1, 311.7))) * 43758.5453);
    float hueShiftR = 1.0 + (hash - 0.5) * 0.10;
    float hueShiftB = 1.0 + (fract(hash * 7.13) - 0.5) * 0.10;
    float brightness = 0.93 + hash * 0.14;
    return vec3(hueShiftR, 1.0, hueShiftB) * brightness;
}

// The lit colour with the crown's depth and the backlight. Normal and position are in view space; heightFraction is the
// fragment's height in the model over the model's height.
vec3 wf_shade(
    in vec3 litColor,
    in vec3 norm,
    in vec3 viewPos,
    in float heightFraction,
    in float exteriorBlend,
    in SceneWideParams scene
) {
    vec3 N = normalize(norm);
    float nUp = clamp(dot(N, normalize(scene.uViewUpSceneTime.xyz)), 0.0, 1.0);
    vec3 shaded = litColor * (0.82 + 0.30 * nUp);
    shaded *= mix(0.55, 1.0, smoothstep(0.0, 0.6, heightFraction));

    vec3 ldir = normalize(-scene.extLight.uExteriorDirectColorDir.xyz);
    vec3 viewDir = normalize(-viewPos);
    float backLit = max(-dot(N, ldir), 0.0);
    float viewDotLight = max(dot(viewDir, -ldir), 0.0);
    float sssAmount = backLit * pow(viewDotLight, 4.0) * 0.35;
    return shaded + sssAmount * vec3(1.0, 0.9, 0.5) * scene.extLight.uExteriorDirectColor.rgb * exteriorBlend;
}

#endif //COMMON_WOWEE_M2_FOLIAGE_SLANG
