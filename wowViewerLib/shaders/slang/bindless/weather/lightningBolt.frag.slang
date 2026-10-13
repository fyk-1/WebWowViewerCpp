#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

// robots@Impl: A port of WoWee's lightning bolt fragment shader (assets/shaders/lightning_bolt.frag.glsl), under the
// licence in ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors.

layout(location = 0) in float vBrightness;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

[shader("pixel")]
void main() {
    vec3 color = mix(vec3(0.6, 0.8, 1.0), vec3(1.0), vBrightness * 0.5);
    outColor = vec4(color, vBrightness);
    writeObjectId(0u);
}
