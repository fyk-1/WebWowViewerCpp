#ifndef COMMON_WOWEE_ADT_LIGHT_SLANG
#define COMMON_WOWEE_ADT_LIGHT_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: A port of WoWee's terrain shader (assets/shaders/terrain.frag.glsl) lighting, in place of calcLight for
// ADT chunks. Kept: flat ambient plus Lambert sun on the blended albedo. Dropped: the derivative bump normal, which
// reads as noise on the ground, and, for lack of the inputs, the layer blend and alpha-map seam blur (the viewer's
// layers, alpha maps and height mix are the inputs and stay), shadow maps, ray-traced light, WoWee's local-light array
// and its fog volume. The light buffer stands in for the local lights, and keeps the retail gamma/linear split so it
// adds the way it always did. Normals are in view space, as the sun direction is, so no world transform is needed.

vec3 wt_calcLight(in vec3 albedo, in vec3 norm, in SceneWideParams scene, in vec3 accumLight, in float ao) {
    vec3 ldir = normalize(-scene.extLight.uExteriorDirectColorDir.xyz);
    float diff = max(dot(norm, ldir), 0.0);

    vec3 gammaTerm = albedo * (scene.extLight.uExteriorAmbientColor.rgb + diff * scene.extLight.uExteriorDirectColor.rgb);
    vec3 linearTerm = (albedo * albedo) * accumLight;
    return sqrt(gammaTerm * gammaTerm + linearTerm) * ao;
}

#endif //COMMON_WOWEE_ADT_LIGHT_SLANG
