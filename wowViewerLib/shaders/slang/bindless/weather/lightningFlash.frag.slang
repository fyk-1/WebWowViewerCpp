#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

// robots@Impl: A port of WoWee's lightning flash fragment shader (assets/shaders/lightning_flash.frag.glsl), under the
// licence in ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. The flash is the first vec4 of the
// lightning buffer, in x.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> lightning;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

[shader("pixel")]
void main() {
    outColor = vec4(1.0, 1.0, 1.0, lightning[0].x * 0.6);
    writeObjectId(0u);
}
