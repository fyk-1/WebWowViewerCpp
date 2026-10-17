#ifndef COMMON_TILE_LIGHT_SLANG
#define COMMON_TILE_LIGHT_SLANG

import glsl;

#include "commonLightFunctions.slang"
#include "commonWoweeSwitch.slang"
#include "commonUboSceneData.slang"

// robots@Impl: The tile light. Config's tileLight, once the host has coloured the grid (TileLightGrid), replaces the scene block's
// exterior colours and the four water colours with the weighted mean of the four tile centres around the fragment's world position, so
// the light changes zone by zone across the map instead of following the camera. Sun direction, sun position and attenuation, the
// sky and the fog stay the block's. The grid is the tileLight buffer: 64 x 64 tiles, row-major, 9 vec4 each (ambient, horizon
// ambient, ground ambient, direct, specular, close river, far river, close ocean, far ocean); the first vec4's w is 1 for a tile
// that has a colouring and 0 for one that has none, which weighs nothing. Rows run along world x and columns along world y, both
// falling, and a tile centre sits at the integer coordinate, so a tile spans one unit around it. A fragment with no coloured tile
// around it keeps the block as it is.

static const float TILE_LIGHT_TILE_SIZE = 533.33333;
static const int TILE_LIGHT_SIDE = 64;
static const int TILE_LIGHT_VEC4S = 9;

SceneWideParams tileLit(SceneWideParams s, vec3 viewPos) {
    if (!woweeOn(s, WOWEE_TILE_LIGHT)) return s;

    vec3 w = (s.uInvLookAtMat * vec4(viewPos, 1.0)).xyz;
    float fr = clamp(32.0 - w.x / TILE_LIGHT_TILE_SIZE - 0.5, 0.0, float(TILE_LIGHT_SIDE - 1));
    float fc = clamp(32.0 - w.y / TILE_LIGHT_TILE_SIZE - 0.5, 0.0, float(TILE_LIGHT_SIDE - 1));
    int r0 = int(floor(fr));
    int c0 = int(floor(fc));
    int r1 = min(r0 + 1, TILE_LIGHT_SIDE - 1);
    int c1 = min(c0 + 1, TILE_LIGHT_SIDE - 1);
    float tr = fr - float(r0);
    float tc = fc - float(c0);

    vec3 ambient = vec3(0.0);
    vec3 horizon = vec3(0.0);
    vec3 ground = vec3(0.0);
    vec3 direct = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec4 closeRiver = vec4(0.0);
    vec4 farRiver = vec4(0.0);
    vec4 closeOcean = vec4(0.0);
    vec4 farOcean = vec4(0.0);
    float total = 0.0;

    for (int k = 0; k < 4; k++) {
        bool lowerRow = (k & 2) == 0;
        bool lowerCol = (k & 1) == 0;
        int base = ((lowerRow ? r0 : r1) * TILE_LIGHT_SIDE + (lowerCol ? c0 : c1)) * TILE_LIGHT_VEC4S;
        float weight = (lowerRow ? 1.0 - tr : tr) * (lowerCol ? 1.0 - tc : tc) * tileLight[base].w;
        if (weight <= 0.0) continue;

        ambient += tileLight[base].rgb * weight;
        horizon += tileLight[base + 1].rgb * weight;
        ground += tileLight[base + 2].rgb * weight;
        direct += tileLight[base + 3].rgb * weight;
        specular += tileLight[base + 4].rgb * weight;
        closeRiver += tileLight[base + 5] * weight;
        farRiver += tileLight[base + 6] * weight;
        closeOcean += tileLight[base + 7] * weight;
        farOcean += tileLight[base + 8] * weight;
        total += weight;
    }
    if (total < 1e-4) return s;

    float inverse = 1.0 / total;
    s.extLight.uExteriorAmbientColor.rgb = ambient * inverse;
    s.extLight.uExteriorHorizontAmbientColor.rgb = horizon * inverse;
    s.extLight.uExteriorGroundAmbientColor.rgb = ground * inverse;
    s.extLight.uExteriorDirectColor.rgb = direct * inverse;
    s.extLight.uExteriorSpecularColor.rgb = specular * inverse;
    s.closeRiverColor_shallowAlpha = closeRiver * inverse;
    s.farRiverColor_deepAlpha = farRiver * inverse;
    s.closeOceanColor_shallowAlpha = closeOcean * inverse;
    s.farOceanColor_deepAlpha = farOcean * inverse;
    return s;
}

#endif //COMMON_TILE_LIGHT_SLANG
