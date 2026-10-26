#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require
#extension GL_EXT_nonuniform_qualifier: require

precision highp float;
precision highp int;

// robots@Impl: A port of WoWee's weather fragment shader (assets/shaders/weather.frag.glsl), under the licence in
// ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. WoWee fades a round point sprite from its
// middle, smoothstep(0.5, 0.2, dist) over a sprite one unit wide. Here the distance is to the particle's streak, a segment
// as long as vLocal.z, in radii, so a dot is the same disc and a streak the same fade around its length. With a picture (the
// slot in weather[1].z, -1 for none) the particle is the picture's texel, tinted by the colour, and no disc fade is applied.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> weather;

layout(set = 2, binding = 0) uniform sampler2D s_Textures[];

layout(location = 0) in vec4 vLocal;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

[shader("pixel")]
void main() {
    vec4 color = weather[0];
    int slot = int(weather[1].z);
    if (slot >= 0) {
        vec4 texel = s_Textures[nonuniformEXT(slot)].Sample(vUv);
        outColor = vec4(color.rgb * texel.rgb, color.a * texel.a);
    } else {
        float onSegment = clamp(vLocal.y, 0.0, vLocal.z);
        float dist = length(vec2(vLocal.x, vLocal.y - onSegment)) / max(vLocal.w, 0.0001) * 0.5;
        if (dist > 0.5) discard;
        outColor = vec4(color.rgb, color.a * smoothstep(0.5, 0.2, dist));
    }
    writeObjectId(0u);
}
