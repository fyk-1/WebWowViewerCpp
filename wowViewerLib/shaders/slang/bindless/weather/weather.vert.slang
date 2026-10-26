#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonUboSceneData.slang"

// robots@Impl: A port of WoWee's weather vertex shader (assets/shaders/weather.vert.glsl), under the licence in
// ../../common/LICENSE-WoWee.txt. Copyright (c) 2026 Wowee Contributors. WoWee draws a point sprite per particle, and a
// pull model draws a quad per instance here, the same size in pixels. The buffer holds the draw's colour, then its look
// (x the size in pixels, y the seconds of fall the streak spans, 0 for a dot, z the slot of the picture in the pass's bindless
// set, -1 for none), then a position and a velocity per particle, the position's w being the particle's lifetime.
// With a picture the quad's UV is its local square: u runs across the streak and v along it, from the head to the tail, so a tall
// picture lies along the fall. A dot turns on the screen at SpinRate radians a second of its lifetime, so flakes started apart turn apart.
// A rain streak runs from the particle back along its velocity on the screen, at most MaxStreak pixels, so a particle
// close to the camera does not cross the view. A position is an offset from the camera, z up, and the shader adds the camera of the frame it draws, which the scene's inverse view
// matrix holds.

layout(std430, set=1, binding=0) StructuredBuffer<vec4> weather;

layout(location = 0) out vec4 vLocal;
layout(location = 1) out vec2 vUv;

const float MaxStreak = 40.0;
const float SpinRate = 1.0;

[shader("vertex")]
void main() {
    vec4 look = weather[1];
    vec3 camera = (scene.uInvLookAtMat * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec4 positionRecord = weather[2 + 2 * gl_InstanceIndex];
    vec3 position = positionRecord.xyz + camera;
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

    vec2 corner = vec2(u * radius, lengthwise);
    if (look.z >= 0.0 && look.y <= 0.0) {
        float angle = positionRecord.w * SpinRate;
        corner = vec2(corner.x * cos(angle) - corner.y * sin(angle), corner.x * sin(angle) + corner.y * cos(angle));
    }

    vec2 pixels = headPixels + along * corner.y + side * corner.x;
    vLocal = vec4(u * radius, lengthwise, streak, radius);
    vUv = vec2(u * 0.5 + 0.5, (lengthwise + radius) / (streak + 2.0 * radius));
    gl_Position = vec4(pixels / halfSize, head.z / head.w, 1.0);
}
