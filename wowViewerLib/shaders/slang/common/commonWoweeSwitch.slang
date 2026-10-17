#ifndef COMMON_WOWEE_SWITCH_SLANG
#define COMMON_WOWEE_SWITCH_SLANG

import glsl;

#include "commonLightFunctions.slang"

// robots@Impl: The runtime switch for the WoWee shader ports. Config's woweeShaders arrives as a bitmask in the
// scene-wide block's uSceneSize_DisableLightBuffer.w; each port's call site tests its own bit and falls back to the
// retail code when it is clear. WOWEE_UNLIT is no port: the bake's unlit frame, set by Config::bakeUnlit, which the forward ADT,
// WMO and M2 shaders answer with their material colour alone. WOWEE_TILE_LIGHT is no port either: Config's tileLight once the tile-light
// grid (set 0, binding 5) has a colouring, which commonTileLight.slang reads.

static const int WOWEE_WATER = 1;
static const int WOWEE_ADT = 2;
static const int WOWEE_WMO = 4;
static const int WOWEE_M2 = 8;
static const int WOWEE_MAGMA = 16;
static const int WOWEE_UNLIT = 32;
static const int WOWEE_TILE_LIGHT = 64;

bool woweeOn(in SceneWideParams scene, in int bit) {
    return (int(scene.uSceneSize_DisableLightBuffer.w + 0.5) & bit) != 0;
}

#endif //COMMON_WOWEE_SWITCH_SLANG
