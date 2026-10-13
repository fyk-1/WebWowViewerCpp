#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

// robots@Impl: A port of WoWee's lightning flash vertex shader (assets/shaders/lightning_flash.vert.glsl), under the
// licence in ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. The quad covers the view and its
// corners come from the vertex index.

[shader("vertex")]
void main() {
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : -1.0, (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0);
    gl_Position = vec4(corner, 0.0, 1.0);
}
