#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonUboSceneData.slang"

// robots@Impl: A port of WoWee's weather vertex shader (assets/shaders/weather.vert.glsl), under the licence in
// ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. WoWee draws a point sprite per particle, and a
// pull model draws a quad per instance here, the same size in pixels. The buffer holds the draw's colour, then its look
// (x the size in pixels, y the seconds of fall the streak spans, 0 for a dot), then a position and a velocity per particle.
// A rain streak runs from the particle back along its velocity on the screen, at most MaxStreak pixels, so a particle
// close to the camera does not cross the view. A position is an offset from the camera, z up, and the shader adds the camera of the frame it draws, which the scene's inverse view
// matrix holds.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> weather;

layout(location = 0) out vec4 vLocal;

const float MaxStreak = 40.0;

[shader("vertex")]
void main() {
    vec4 look = weather[1];
    vec3 camera = (scene.uInvLookAtMat * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 position = weather[2 + 2 * gl_InstanceIndex].xyz + camera;
    vec3 velocity = weather[3 + 2 * gl_InstanceIndex].xyz;

    mat4 viewProjection = scene.uPMatrix * scene.uLookAtMat;
    vec4 head = viewProjection * vec4(position, 1.0);
    vec4 tail = viewProjection * vec4(position - velocity * look.y, 1.0);

    // Behind the camera there is nothing to draw: a corner outside the clip volume drops the quad.
    if (head.w < 0.1) {
        vLocal = vec4(0.0);
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec2 halfSize = 0.5 * scene.uSceneSize_DisableLightBuffer.xy;
    vec2 headPixels = head.xy / head.w * halfSize;
    vec2 tailPixels = tail.w < 0.1 ? headPixels : tail.xy / tail.w * halfSize;

    vec2 along = tailPixels - headPixels;
    float streak = min(sqrt(dot(along, along)), MaxStreak);
    along = streak > 0.001 ? normalize(tailPixels - headPixels) : vec2(0.0, 1.0);
    vec2 side = vec2(-along.y, along.x);

    float radius = 0.5 * look.x;
    float u = (gl_VertexIndex & 1) != 0 ? 1.0 : -1.0;
    float v = (gl_VertexIndex & 2) != 0 ? 1.0 : -1.0;
    // v of -1 starts a radius before the head, and v of 1 ends a radius past the tail.
    float lengthwise = v < 0.0 ? -radius : streak + radius;

    vec2 pixels = headPixels + along * lengthwise + side * (u * radius);
    vLocal = vec4(u * radius, lengthwise, streak, radius);
    gl_Position = vec4(pixels / halfSize, head.z / head.w, 1.0);
}
