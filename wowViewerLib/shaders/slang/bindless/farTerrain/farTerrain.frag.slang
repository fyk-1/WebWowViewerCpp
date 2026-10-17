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
// 4 other). A texel of kind 1 or 2 takes the frame's far river or far ocean colour, lit as the water lights it (farLiquidLit), and the fog below applies
// to it as to any far fragment; the other kinds keep the lit ground. The kind is fetched from the nearest texel, since the filtered
// sample blends the kinds of two neighbours at a shore, and the filtered rgb stays the ground's. The minimap is a picture already
// shaded by its relief, and lighting its slope again shaded it twice, the far land darker than the full chunks beside it, so it is
// lit flat, on the view's up. Both are fogged in two steps. First the scene's fog, as the full chunks take it, but at a distance
// held to the draw distance: the scene's fog would paint all of this land in fog colour past it, and held there it gives every far
// fragment what a full chunk at the seam along the same ray gets, so the land runs on across the seam in the same haze. Then the far
// pass's own fog on top: toward the frame's end fog colour by distance from the draw distance to the far plane, times the strength.
// A tile whose texture has not landed is the fog colour outright, so the gap reads as haze. The draw that takes pixels from the
// fading full scene keeps the fragments the scene's screen-door drops (dropsForCullFade, the pattern of Config::m2FadeStartPercent),
// and writes no depth in the others, which is why this shader tests depth after it runs: a discard must not leave a depth write
// behind. A landmark (FarLandmarks) wears its model's texture through the same path, lit and fogged alike. With the tile light on, the ground's
// lighting and the far river and ocean colours are the tile's at the fragment's world position (tileLit), and the fog stays the block's.

layout(std140, set=1, binding=0) uniform farTerrainBlock {
    mat4 uFarViewProjection;
    vec4 uFarParams;
    vec4 uFarFogColor;
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

    vec3 fogColor = uFarFogColor.xyz;
    vec3 color = fogColor;

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
        vec3 lit;
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

        float dist = length(vPosition);
        vec3 atSeam = vPosition * (min(dist, uFarParams.x) / max(dist, 0.001));
        vec3 hazed = makeFog2(fogData, vec4(lit, 1.0), scene.uViewUpSceneTime.xyz, atSeam,
                              scene.extLight.uExteriorDirectColorDir.xyz, 0).rgb;

        float span = max(uFarParams.y - uFarParams.x, 1.0);
        float fog = clamp((dist - uFarParams.x) / span * uFarParams.z, 0.0, 1.0);
        color = mix(hazed, fogColor, fog);
    }

    outColor = vec4(color, 1.0);
    writeObjectId(0u);
}
