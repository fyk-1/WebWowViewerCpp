#version 450

import glsl;

#extension GL_GOOGLE_include_directive: require

precision highp float;
precision highp int;

#include "../../common/commonUboSceneData.slang"

// robots@Impl: The far terrain's vertex shader. The far pass has a projection of its own, with the depth range folded in, and the pass
// draws through the map-area viewport, so the far tiles land in a band of depth past everything the scene draws and short of the sky
// (FarTerrain's band of depth has the range and why). The matrix is in a block of its own, never in the scene-wide one. It holds the
// view's rotation without its translation and the shader takes the camera off the vertex first, since a world position of 17 000
// yards through a matrix carrying the camera's translation loses a thousandth of a yard of depth to the float, and the same
// vertex through the rotation alone loses a ten-thousandth at 2 000 yards. The instance
// index of the draw is two flags: bit 0 is the tile's texture flag, 1 where the minimap has landed and 0 where it has not, and bit 1
// marks the draw that takes pixels from the fading full scene (FarTerrain's fade of the swap), which maps the same depth into a
// band just above 0, through the usual viewport, so that it lies in front of every scene fragment and the far fragments still order
// among themselves; so a tile costs no block of its own. Light and fog are the fragment shader's, from the view-space position and
// normal.

layout(std140, set=1, binding=0) uniform farTerrainBlock {
    mat4 uFarViewProjection;
    // x: the distance the fog starts at, y: the far plane, z: the fog's strength.
    vec4 uFarParams;
    vec4 uFarFogColor;
    // x: the share of the full scene that shows, y: where the taking draw's depth band starts and z: how wide it is, in NDC.
    vec4 uFarFade;
    // xyz: the camera in the world, which the vertices are taken relative to.
    vec4 uFarCamera;
};

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec3 vPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out flat int vFlags;

[shader("vertex")]
void main() {
    vec3 relative = aPosition - uFarCamera.xyz;

    vPosition = (scene.uLookAtMat * vec4(relative, 0.0)).xyz;
    vNormal = (scene.uLookAtMat * vec4(aNormal, 0.0)).xyz;
    vTexCoord = aTexCoord;
    vFlags = gl_InstanceIndex;

    gl_Position = uFarViewProjection * vec4(relative, 1.0);
    if ((gl_InstanceIndex & 2) != 0) {
        gl_Position.z = (uFarFade.y + uFarFade.z * (gl_Position.z / gl_Position.w)) * gl_Position.w;
    }
}
