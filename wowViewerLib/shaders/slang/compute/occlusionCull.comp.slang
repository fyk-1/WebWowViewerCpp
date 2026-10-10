import glsl;

#extension GL_EXT_debug_printf : enable

struct AABB {
    float4 min;
    float4 max;
};

struct OcclusionCullConstants {
    float4x4 viewProj;
    uint objectCount;
    uint hiZMipCount;
    uint hiZWidth;
    uint hiZHeight;
    float viewportDepthMin;  // viewport minDepth (e.g., 0.06)
    float viewportDepthRange; // viewport maxDepth - minDepth (e.g., 0.94)
    float pad0;
    float pad1;
};

[vk::push_constant]
OcclusionCullConstants constants;

StructuredBuffer<AABB, ScalarDataLayout> aabbBuffer;
RWStructuredBuffer<uint, ScalarDataLayout> visibilityBuffer;
Sampler2D hiZMap;

[shader("compute")]
[numthreads(64, 1, 1)]
void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= constants.objectCount) return;

    float3 aabbMin = aabbBuffer[idx].min.xyz;
    float3 aabbMax = aabbBuffer[idx].max.xyz;

    // Generate 8 AABB corners
    float4 corners[8];
    corners[0] = float4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = float4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = float4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = float4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = float4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = float4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = float4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = float4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    // ---- Frustum Culling (clip space) ----
    uint allOutside_left   = 0xff;
    uint allOutside_right  = 0xff;
    uint allOutside_bottom = 0xff;
    uint allOutside_top    = 0xff;
    uint allOutside_near   = 0xff;
    uint allOutside_far    = 0xff;
    bool anyBehindCamera   = false;

    // For occlusion test: track screen-space bounds and nearest depth (in depth buffer space)
    float2 ndcMin = float2(1e30, 1e30);
    float2 ndcMax = float2(-1e30, -1e30);
    float nearestDepth = 1.0; // depth buffer space, regular Z: smallest corner value = nearest

    for (uint i = 0; i < 8; i++) {
        float4 clip = constants.viewProj * corners[i];

        if (clip.w <= 0.0) {
            anyBehindCamera = true;
            break;
        }

        uint bit = 1u << i;
        if (clip.x >= -clip.w) allOutside_left   &= ~bit;
        if (clip.x <=  clip.w) allOutside_right  &= ~bit;
        if (clip.y >= -clip.w) allOutside_bottom  &= ~bit;
        if (clip.y <=  clip.w) allOutside_top    &= ~bit;
        if (clip.z >= 0.0)     allOutside_near   &= ~bit;
        if (clip.z <=  clip.w) allOutside_far    &= ~bit;

        // Perspective divide
        float3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);

        // Convert NDC depth to depth buffer space: depthBuf = minDepth + ndc_z * range
        float depthBufValue = constants.viewportDepthMin + ndc.z * constants.viewportDepthRange;
        nearestDepth = min(nearestDepth, depthBufValue); // regular Z: nearest = smallest value
    }

    if (anyBehindCamera) {
        visibilityBuffer[idx] = 1u;
        return;
    }

    bool frustumCulled = (allOutside_left   == 0xff) || (allOutside_right == 0xff) ||
                         (allOutside_bottom == 0xff) || (allOutside_top   == 0xff) ||
                         (allOutside_near   == 0xff) || (allOutside_far   == 0xff);

    if (frustumCulled) {
        visibilityBuffer[idx] = 0u;
        return;
    }

    // ---- Hi-Z Occlusion Test ----
    // Clamp NDC to [-1, 1]
    ndcMin = clamp(ndcMin, float2(-1.0, -1.0), float2(1.0, 1.0));
    ndcMax = clamp(ndcMax, float2(-1.0, -1.0), float2(1.0, 1.0));

    // Convert NDC to UV [0,1]
    float2 uvMin = ndcMin * 0.5 + 0.5;
    float2 uvMax = ndcMax * 0.5 + 0.5;

    // Screen-space coverage ratio — skip occlusion for objects covering >50% of screen
    float2 uvExtent = uvMax - uvMin;
    if (uvExtent.x > 0.5 || uvExtent.y > 0.5) {
        visibilityBuffer[idx] = 1u;
        return;
    }

    // Screen-space size in pixels at mip 0
    float2 screenSize = float2(constants.hiZWidth, constants.hiZHeight);
    float2 pixelSize = uvExtent * screenSize;

    // Choose mip level where AABB covers roughly 2x2 texels (conservative)
    float maxPixelDim = max(pixelSize.x, pixelSize.y)/2;
    if (maxPixelDim < 1.0) {
        // Sub-pixel object — too small to reliably test
        visibilityBuffer[idx] = 1u;
        return;
    }
    // Pick mip level where AABB covers ~2x2 texels for better doorway/opening resolution
    float mipF = log2(maxPixelDim)+1;
    int mipLevel = clamp(int(ceil(mipF)), 0, int(constants.hiZMipCount) - 1);

    // Regular Z (LESS_OR_EQUAL): Hi-Z construction uses MAX (farthest surface per region, largest value).
    // Sample 4 corners and take MAX again = weakest occluder.
    // If the object is behind even the weakest occluder → definitely fully occluded.
    float hiZDepth = textureLod(hiZMap, uvMin, float(mipLevel)).x;
    hiZDepth = max(hiZDepth, textureLod(hiZMap, float2(uvMax.x, uvMin.y), float(mipLevel)).x);
    hiZDepth = max(hiZDepth, textureLod(hiZMap, float2(uvMin.x, uvMax.y), float(mipLevel)).x);
    hiZDepth = max(hiZDepth, textureLod(hiZMap, uvMax, float(mipLevel)).x);

    // Regular Z: smaller depth = nearer, larger = farther.
    // hiZDepth (max of MAX) = weakest occluder (the farthest surface in the covered region).
    // If object's nearest depth (smallest corner value) is still larger (farther)
    // than even the weakest occluder → object is fully behind → occluded.
    bool occluded = nearestDepth > hiZDepth;

	if (occluded) {
/*
		debugPrintfEXT("idx= %u hiZMipCount = %u pixelSize = (%1.5f,%1.5f) mipF = %1.5f mipLevel = %d nearestDepth = %1.5f hiZDepth = %1.5f uvMin = (%1.5f,%1.5f) uvMax = (%1.5f,%1.5f)",
		idx, constants.hiZMipCount, pixelSize.x, pixelSize.y, mipF, mipLevel, nearestDepth, hiZDepth, uvMin.x, uvMin.y, uvMax.x, uvMax.y);
*/
	}

    visibilityBuffer[idx] = occluded ? 0u : 1u;
}
