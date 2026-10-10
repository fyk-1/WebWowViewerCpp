#ifndef COMMON_WOWEE_WMO_LIGHT_SLANG
#define COMMON_WOWEE_WMO_LIGHT_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: A port of WoWee's WMO shader (assets/shaders/wmo.frag.glsl) lighting, in place of calcLight for WMO
// groups. Kept: the interior model, where baked vertex light floors the surface and the sun never reaches it; the
// exterior model, flat ambient plus Lambert sun plus a Blinn highlight, with the vertex light added at half strength;
// the unlit case. The viewer's per-group interior/exterior blend picks between the two, as WoWee's per-group flag does.
// Dropped, for lack of the inputs: normal maps and parallax (WoWee derives them from the diffuse on the CPU), window
// glass, lava and the lamp/clock emissive hacks (flags the viewer's batches do not carry), shadow maps, ray-traced light,
// local-light array, fog volume and the back-face normal flip. The viewer's vertex light arrives with the group's
// ambient already subtracted, so WoWee's max(vertex light, ambient) floor reads as their sum here. The light buffer
// stands in for the local lights and keeps the retail gamma/linear split; combiner emissive and specular still add.

vec3 wo_calcLight(
    in vec3 albedo,
    in vec3 norm,
    in vec3 viewPos,
    in bool applyLight,
    in SceneWideParams scene,
    in InteriorLightParam intLight,
    in vec3 accumLight,
    in vec3 vertexLight,
    in vec3 specular,
    in vec3 emissive
) {
    if (!applyLight)
        return albedo + specular;

    float exteriorBlend = intLight.uInteriorAmbientColorAndInteriorExteriorBlend.w;
    vec3 interiorTerm = albedo * max(vertexLight + intLight.uInteriorAmbientColorAndInteriorExteriorBlend.rgb, vec3(0.15));

    vec3 exteriorTerm = vec3(0.0);
    vec3 highlight = vec3(0.0);
    if (exteriorBlend > 0.0) {
        vec3 N = normalize(norm);
        vec3 ldir = normalize(-scene.extLight.uExteriorDirectColorDir.xyz);
        vec3 viewDir = normalize(-viewPos);
        vec3 halfDir = normalize(ldir + viewDir);
        float diff = max(dot(N, ldir), 0.0);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.5;

        exteriorTerm = albedo * (scene.extLight.uExteriorAmbientColor.rgb + diff * scene.extLight.uExteriorDirectColor.rgb)
                     + albedo * vertexLight * 0.5;
        highlight = exteriorBlend * spec * scene.extLight.uExteriorDirectColor.rgb;
    }

    vec3 gammaTerm = mix(interiorTerm, exteriorTerm, exteriorBlend);
    vec3 linearTerm = (albedo * albedo) * accumLight;
    return sqrt(gammaTerm * gammaTerm + linearTerm) + emissive + highlight + specular;
}

#endif //COMMON_WOWEE_WMO_LIGHT_SLANG
