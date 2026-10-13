#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

// robots@Impl: A port of WoWee's weather fragment shader (assets/shaders/weather.frag.glsl), under the licence in
// ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. WoWee fades a round point sprite from its
// middle, smoothstep(0.5, 0.2, dist) over a sprite one unit wide. Here the distance is to the particle's streak, a segment
// as long as vLocal.z, in radii, so a dot is the same disc and a streak the same fade around its length.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> weather;

layout(location = 0) in vec4 vLocal;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

[shader("pixel")]
void main() {
    vec4 color = weather[0];
    float onSegment = clamp(vLocal.y, 0.0, vLocal.z);
    float dist = length(vec2(vLocal.x, vLocal.y - onSegment)) / max(vLocal.w, 0.0001) * 0.5;
    if (dist > 0.5) discard;
    outColor = vec4(color.rgb, color.a * smoothstep(0.5, 0.2, dist));
    writeObjectId(0u);
}
