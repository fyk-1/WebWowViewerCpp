#version 450

import glsl;

precision highp float;
precision highp int;

layout(location = 0) in vec2 texCoord;

layout(set=1, binding=0) uniform sampler2D screenTex;

layout(location = 0) out vec4 outColor;

// Draws the FSR output onto the swapchain, stretched with linear filtering when fsrOutputScale is below 1.
[shader("pixel")]
void main() {
    outColor = vec4(texture(screenTex, texCoord).rgb, 1.0);
}
