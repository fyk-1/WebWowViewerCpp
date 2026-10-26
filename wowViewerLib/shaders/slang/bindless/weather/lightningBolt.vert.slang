#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonUboSceneData.slang"

// robots@Impl: A port of WoWee's lightning bolt vertex shader (assets/shaders/lightning_bolt.vert.glsl), under the licence
// in ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. WoWee draws a bolt as one line strip, and here
// every link of every bolt is a line of its own, so a branch does not join the main chain. The buffer holds
// two vec4 per link, the start with the bolt's brightness in w and the end after it.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> lightning;

layout(location = 0) out float vBrightness;

[shader("vertex")]
void main() {
    vec4 from = lightning[2 * gl_InstanceIndex];
    vec4 to = lightning[1 + 2 * gl_InstanceIndex];
    vec3 position = gl_VertexIndex == 0 ? from.xyz : to.xyz;

    vBrightness = from.w;
    gl_Position = scene.uPMatrix * scene.uLookAtMat * vec4(position, 1.0);
}
