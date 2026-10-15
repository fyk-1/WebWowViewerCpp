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
#include "../../common/commonCullFade.slang"

// robots@Impl: The far terrain's fragment shader. A tile wearing its minimap is lit as a flat ADT chunk is, by the exterior ambient
// and sun on the vertex normal, with no specular, layers or light buffer, and fogged by the far pass's own fog: toward the frame's end
// fog colour by distance from the draw distance to the far plane, times the strength. The scene's fog is not applied, since it ends at
// the draw distance and would paint all of this land in fog colour. A tile whose minimap has not landed is the fog colour outright, so
// the gap reads as haze. The draw that takes pixels from the fading full scene keeps the fragments the scene's screen-door drops
// (dropsForCullFade, the pattern of Config::m2CullFadeBand), and writes no depth in the others, which is why this shader tests depth
// after it runs: a discard must not leave a depth write behind.

layout(std140, set=1, binding=0) uniform farTerrainBlock {
    mat4 uFarViewProjection;
    vec4 uFarParams;
    vec4 uFarFogColor;
    vec4 uFarFade;
    vec4 uFarCamera;
};

layout(set=2, binding=0) uniform sampler2D uMinimap;

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
        vec3 matDiffuse = texture(uMinimap, vTexCoord).rgb;
        vec3 normal = normalize(vNormal);

        vec3 lit;
        if (woweeOn(scene, WOWEE_ADT)) {
            lit = wt_calcLight(matDiffuse, normal, scene, vec3(0.0), 1.0);
        } else {
            lit = calcLight(matDiffuse, normal, vPosition, true, scene, intLight,
                            vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0);
        }

        float span = max(uFarParams.y - uFarParams.x, 1.0);
        float fog = clamp((length(vPosition) - uFarParams.x) / span * uFarParams.z, 0.0, 1.0);
        color = mix(lit, fogColor, fog);
    }

    outColor = vec4(color, 1.0);
    writeObjectId(0u);
}
