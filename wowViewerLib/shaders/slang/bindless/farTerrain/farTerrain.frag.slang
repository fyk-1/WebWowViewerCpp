#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonLightFunctions.slang"
#include "../../common/commonFogFunctions.slang"
#include "../../common/commonWoweeAdtLight.slang"
#include "../../common/commonWoweeSwitch.slang"
#include "../../common/commonUboSceneData.slang"
#include "../../common/commonTileLight.slang"
#include "../../common/commonCullFade.slang"

// robots@Impl: The far terrain's fragment shader. A tile wears one of two pictures of the ground and is lit by the exterior ambient
// and sun as an ADT chunk is, with no specular, layers or light buffer. The baked colour (bit 5) is the ground's unlit albedo, so it
// is lit on the slope's own normal like a full chunk. Its alpha is the liquid kind of the texel (1 river or lake, 2 ocean, 3 magma,
// 4 other). A texel of kind 1 or 2 takes the frame's far river or far ocean colour, lit as the water lights it (farLiquidLit); the
// other kinds keep the lit ground. The kind is fetched from the nearest texel, since the filtered sample blends the kinds of two
// neighbours at a shore, and the filtered rgb stays the ground's. The minimap is a picture already shaded by its relief, and lighting
// its slope again shaded it twice, the far land darker than the full chunks beside it, so it is lit flat, on the view's up. Every
// fragment takes one fog path, makeFog2, the function every scene fragment takes: the zone's ground fog at the fragment's true
// distance (the classic fog's falloff thins it with the camera's height), and the far fog on top toward the sky's fog colour. A tile whose texture
// has not landed is a fragment of that colour through the same path, so the gap reads as haze. The draw that takes pixels from
// the fading full scene keeps the fragments the scene's screen-door drops (dropsForCullFade, the pattern of Config::m2FadeStartPercent),
// and writes no depth in the others, which is why this shader tests depth after it runs: a discard must not leave a depth write
// behind. A landmark (FarLandmarks) wears its model's texture through the same path, lit and fogged alike. With the tile light on, the
// ground's lighting and the far river and ocean colours are the tile's at the fragment's world position (tileLit).

layout(std140, set=1, binding=0) uniform farTerrainBlock {
    mat4 uFarViewProjection;
    vec4 uFarFade;
    vec4 uFarCamera;
};

// The tile's ground: its minimap, or its baked colour (bit 5).
layout(set=2, binding=0) uniform sampler2D uGround;

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in flat int vFlags;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

const InteriorLightParam intLight = {
    vec4(0,0,0,1.0f),
    vec4(0,0,0,0),
    vec4(0,0,0,0),
    vec4(0,0,0,1),
    vec4(0,0,0,0)
};

[shader("pixel")]
void main() {
    if ((vFlags & 2) != 0 && !dropsForCullFade(uFarFade.x, gl_FragCoord.xy)) discard;

    // A tile whose texture has not landed stays the far fog colour, and takes the fog like any other fragment.
    vec3 lit = validateFogColor(fogData.farFogColor_rate.xyz, 0);

    if ((vFlags & 1) != 0) {
        // 🤖 The baked colour's sample i lies i / 127 of the way along the tile, as the heights' do, at texel centre (i + 0.5) / 128.
        vec2 groundUv = (vFlags & 32) != 0 ? vTexCoord * (127.0 / 128.0) + 0.5 / 128.0 : vTexCoord;
        vec4 texel = texture(uGround, groundUv);
        // A landmark (bit 3) with an alpha-keyed material (bit 4) cuts its transparent texels away.
        if ((vFlags & 16) != 0 && texel.a < 0.5) discard;
        vec3 matDiffuse = texel.rgb;
        int liquidKind = 0;
        if ((vFlags & 32) != 0) {
            ivec2 nearest = clamp(ivec2(floor(groundUv * 128.0)), ivec2(0), ivec2(127));
            liquidKind = int(texelFetch(uGround, nearest, 0).a * 255.0 + 0.5);
        }
        // A landmark (bit 3) wears its model's texture, and a tile its baked colour (bit 5): both are lit on their own normal.
        vec3 normal = (vFlags & (8 | 32)) != 0 ? normalize(vNormal) : normalize(scene.uViewUpSceneTime.xyz);

        SceneWideParams tile = tileLit(scene, vPosition);
        if (liquidKind == 1) {
            lit = farLiquidLit(tile, tile.farRiverColor_deepAlpha.rgb);
        } else if (liquidKind == 2) {
            lit = farLiquidLit(tile, tile.farOceanColor_deepAlpha.rgb);
        } else if (woweeOn(scene, WOWEE_ADT)) {
            lit = wt_calcLight(matDiffuse, normal, tile, vec3(0.0), 1.0);
        } else {
            lit = calcLight(matDiffuse, normal, vPosition, true, tile, intLight,
                            vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0);
        }
    }

    vec3 color = makeFog2(fogData, vec4(lit, 1.0), scene.uViewUpSceneTime.xyz, vPosition,
                          scene.extLight.uExteriorDirectColorDir.xyz, 0).rgb;

    outColor = vec4(color, 1.0);
    writeObjectId(0u);
}
