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
// index of the draw is a set of flags: bit 0 is the tile's texture flag, 1 where its texture (the minimap or the baked colour) has
// landed and 0 where it has not, bit 5 marks a tile wearing its baked colour, and bit 1
// marks the draw that takes pixels from the fading full scene (FarTerrain's fade of the swap), which maps the same depth into a
// band just above 0, through the usual viewport, so that it lies in front of every scene fragment and the far fragments still order
// among themselves; so a tile costs no block of its own. Bit 2 marks a tile drawn on the shared grid, whose vertices are offsets from
// the tile's corner in world orientation: the tile's row (bits 8 to 13) and column (bits 14 to 19) place the corner, the grid's level (bits 20 and 21) is
// how coarse it is, and the baked
// height texture, 128 x 128 samples with the first and last on the tile's edges, gives each vertex its z and the normal from the four
// samples around it. Light and fog are the fragment shader's, from the view-space position and normal.

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

// The baked heights of the tile, set 2's second binding; the white pixel where the tile has none.
layout(set=2, binding=1) uniform sampler2D uHeights;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec3 vPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out flat int vFlags;

// The size of a map tile in yards, and the baked height's unit: a sample's z is its unorm value times 65535 / 16, less 1024 yards.
const float kTileSize = 533.33333;
const float kHeightsPerUnorm = 65535.0 / 16.0;
const float kHeightsBase = 1024.0;
const float kHeightsSize = 128.0;

[shader("vertex")]
void main() {
    vec3 relative = aPosition - uFarCamera.xyz;
    vec3 normal = aNormal;

    if ((gl_InstanceIndex & 4) != 0) {
        float row = float((gl_InstanceIndex >> 8) & 63);
        float col = float((gl_InstanceIndex >> 14) & 63);
        vec2 corner = vec2((32.0 - row) * kTileSize, (32.0 - col) * kTileSize);

        // The vertex sits on the lattice of the heights: sample i lies i / 127 of the way along the tile, at texel centre (i + 0.5) / 128.
        vec2 uv = aTexCoord * ((kHeightsSize - 1.0) / kHeightsSize) + 0.5 / kHeightsSize;
        // A coarser grid (the level, bits 20 and 21) takes its slope over as many samples as its cell spans, so it shades as smoothly
        // as its geometry.
        float step = float(2 << ((gl_InstanceIndex >> 20) & 3));
        float texel = step * 0.5 / kHeightsSize;
        float height = textureLod(uHeights, uv, 0.0).r * kHeightsPerUnorm - kHeightsBase;

        // The slope along the rows (v, world x falling) and the columns (u, world y falling), in yards a yard, as the tile's own normals are.
        float spacing = step * kTileSize / (kHeightsSize - 1.0);
        float alongRows = (textureLod(uHeights, uv + vec2(0.0, texel), 0.0).r - textureLod(uHeights, uv - vec2(0.0, texel), 0.0).r) * kHeightsPerUnorm / spacing;
        float alongCols = (textureLod(uHeights, uv + vec2(texel, 0.0), 0.0).r - textureLod(uHeights, uv - vec2(texel, 0.0), 0.0).r) * kHeightsPerUnorm / spacing;
        normal = normalize(vec3(alongRows, alongCols, 1.0));

        relative = vec3(corner + aPosition.xy - uFarCamera.xy, height + aPosition.z - uFarCamera.z);
    }

    vPosition = (scene.uLookAtMat * vec4(relative, 0.0)).xyz;
    vNormal = (scene.uLookAtMat * vec4(normal, 0.0)).xyz;
    vTexCoord = aTexCoord;
    vFlags = gl_InstanceIndex;

    gl_Position = uFarViewProjection * vec4(relative, 1.0);
    if ((gl_InstanceIndex & 2) != 0) {
        gl_Position.z = (uFarFade.y + uFarFade.z * (gl_Position.z / gl_Position.w)) * gl_Position.w;
    }
}
