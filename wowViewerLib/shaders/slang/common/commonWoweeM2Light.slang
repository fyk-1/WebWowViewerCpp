#ifndef COMMON_WOWEE_M2_LIGHT_SLANG
#define COMMON_WOWEE_M2_LIGHT_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: A port of WoWee's M2 shader (assets/shaders/m2.frag.glsl) lighting, in place of calcLight for M2
// meshes. Kept: flat ambient plus Lambert sun on the combiner albedo, and the Blinn highlight. Alpha-keyed meshes stand
// in for WoWee's foliage class, which has no flag in the viewer: they light two-sided and take no highlight. The
// viewer's interior/exterior blend still decides how much of the model is WoWee-lit; the interior share keeps the
// retail calcLight, since WoWee has no interior lighting for M2 beyond a darken it never sets. Dropped, for lack of the
// inputs: foliage hue shift, sway, translucency and height occlusion, volumetric beam and fire card, the sky mode,
// shadow maps, ray-traced light, local-light array, fog volume and the back-face normal flip. The light buffer stands in
// for the local lights and keeps the retail gamma/linear split; combiner specular still adds. ambientScale multiplies
// the exterior ambient term alone; only the foliage sky bounce passes other than 1.

vec3 wm_calcLight(
    in vec3 albedo,
    in vec3 norm,
    in vec3 viewPos,
    in bool applyLight,
    in bool alphaKeyed,
    in SceneWideParams scene,
    in InteriorLightParam intLight,
    in vec3 accumLight,
    in vec3 specular,
    in float ambientScale
) {
    if (!applyLight)
        return albedo + specular;

    float exteriorBlend = intLight.uInteriorAmbientColorAndInteriorExteriorBlend.w;

    vec3 exteriorResult = vec3(0.0);
    if (exteriorBlend > 0.0) {
        vec3 N = normalize(norm);
        vec3 ldir = normalize(-scene.extLight.uExteriorDirectColorDir.xyz);
        vec3 halfDir = normalize(ldir + normalize(-viewPos));
        float nDotL = dot(N, ldir);
        float diff = alphaKeyed ? abs(nDotL) : max(nDotL, 0.0);
        float spec = alphaKeyed ? 0.0 : pow(max(dot(N, halfDir), 0.0), 32.0) * 0.5;

        vec3 gammaTerm = albedo * (scene.extLight.uExteriorAmbientColor.rgb * ambientScale + diff * scene.extLight.uExteriorDirectColor.rgb);
        vec3 linearTerm = (albedo * albedo) * accumLight;
        exteriorResult = sqrt(gammaTerm * gammaTerm + linearTerm) + spec * scene.extLight.uExteriorDirectColor.rgb;
    }

    vec3 interiorResult = vec3(0.0);
    if (exteriorBlend < 1.0) {
        InteriorLightParam interiorOnly = intLight;
        interiorOnly.uInteriorAmbientColorAndInteriorExteriorBlend.w = 0.0;
        interiorResult = calcLight(albedo, norm, viewPos, true, scene, interiorOnly, accumLight, vec3(0.0), vec3(0.0), vec3(0.0), 1.0);
    }

    return mix(interiorResult, exteriorResult, exteriorBlend) + specular;
}

#endif //COMMON_WOWEE_M2_LIGHT_SLANG
