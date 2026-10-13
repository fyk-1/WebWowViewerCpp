#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

// robots@Impl: The road recorder's ghost line (RoadLines.h): the baked roads in a faint cyan, the running take in an orange.

layout(location = 0) in float vKind;

layout(location = 0) out vec4 outColor;
#include "../../common/commonObjectId.slang"

[shader("pixel")]
void main() {
    outColor = vKind > 0.5 ? vec4(1.0, 0.55, 0.1, 0.95) : vec4(0.4, 0.9, 1.0, 0.5);
    writeObjectId(0u);
}
