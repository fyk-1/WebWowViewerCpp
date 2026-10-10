import glsl;

// robots@Impl: Camera motion vectors and a standard device depth for the FSR upscaler, both at render resolution.
//
// The GBuffer depth is unprojected with the current unjittered view-projection and reprojected with the previous
// frame's. The vector is previous UV minus current UV, the direction FSR expects, and has no per-object part.
// The depth copy is the view-space depth turned into the [0, 1] device depth of a standard projection for the
// camera's near and far, since the scene's own depth sits in an offset range that FSR would linearize wrongly.

struct MotionVectorConstants {
    // Previous view-projection * inverse of the current one, so it maps current NDC to previous clip space.
    float4x4 reproject;
    // Fourth row of that inverse of the current view-projection: its dot with NDC is 1 / view depth.
    float4 invViewProjRowW;
    float4 nearFarDepthMinDepthRange; // x: camera near, y: camera far, z, w: viewport depth range start and size
    float4 sizeFarNdcZ;               // x, y: render size, z: NDC depth of the far plane
};

[vk::push_constant]
MotionVectorConstants constants;

Sampler2D depthSrc;

[vk::image_format("rg16f")]
RWTexture2D<float2> motionDst;

[vk::image_format("r32f")]
RWTexture2D<float> depthDst;

[shader("compute")]
[numthreads(8, 8, 1)]
void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    vec2 size = constants.sizeFarNdcZ.xy;

    if (coord.x >= int(size.x) || coord.y >= int(size.y)) return;

    vec2 uv = (vec2(coord) + 0.5) / size;
    float rawDepth = textureLod(depthSrc, uv, 0.0).x;

    float depthMin = constants.nearFarDepthMinDepthRange.z;
    float depthRange = constants.nearFarDepthMinDepthRange.w;

    // Pixels the geometry never wrote hold the clear value, the end of the viewport range. They are the sky
    // and sit at the far plane.
    bool isSky = rawDepth >= depthMin + depthRange - 1e-5;
    float ndcZ = isSky ? constants.sizeFarNdcZ.z : (rawDepth - depthMin) / depthRange;

    vec4 ndc = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);

    float invViewDepth = dot(constants.invViewProjRowW, ndc);
    float viewDepth = invViewDepth > 0.0 ? 1.0 / invViewDepth : constants.nearFarDepthMinDepthRange.y;

    vec4 prevClip = constants.reproject * ndc;
    vec2 motion = vec2(0.0, 0.0);
    if (prevClip.w > 0.0) {
        motion = (prevClip.xy / prevClip.w) * 0.5 + 0.5 - uv;
    }

    float nearPlane = constants.nearFarDepthMinDepthRange.x;
    float farPlane = constants.nearFarDepthMinDepthRange.y;
    float deviceDepth = isSky
        ? 1.0
        : clamp(farPlane * (viewDepth - nearPlane) / (viewDepth * (farPlane - nearPlane)), 0.0, 1.0);

    motionDst[coord] = motion;
    depthDst[coord] = deviceDepth;
}
