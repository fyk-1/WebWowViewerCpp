#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonUboSceneData.slang"

// robots@Impl: The road recorder's ghost line (RoadLines.h). The buffer holds two vec4 for each segment, the start with the segment's kind in
// w (0 for a road, 1 for the take) and the end after it, drawn as one line from the vertex index and the instance index.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> roadLines;

layout(location = 0) out float vKind;

[shader("vertex")]
void main() {
    vec4 from = roadLines[2 * gl_InstanceIndex];
    vec4 to = roadLines[1 + 2 * gl_InstanceIndex];
    vec3 position = gl_VertexIndex == 0 ? from.xyz : to.xyz;

    vKind = from.w;
    gl_Position = scene.uPMatrix * scene.uLookAtMat * vec4(position, 1.0);
}
