//
// Created by Deamon on 29.09.22.
//

#ifndef AWEBWOWVIEWERCPP_MATHHELPER_CULLING_H
#define AWEBWOWVIEWERCPP_MATHHELPER_CULLING_H

#include <array>
#include "mathHelper.h"
#include "../../include/config.h"

template<typename T>
class ObjectCulling {
public:
    static void cull(const MathHelper::FrustumCullingData &cullingData,
                     const int start,
                     const int end,
                     const framebased::vector<T> &objects,
                     std::vector<uint32_t> &culling_res) {
        for (int i = start; i < end; i++) {
            culling_res[i] &= MathHelper::checkFrustum(cullingData.frustums, retrieveAABB(objects[i])) ? 0 : 0xFFFFFFFF;
        }
    }
};

// Marks culled the visible objects whose bounding sphere spans less than minScreenPercent of the view's height,
// seen from cameraPos with tanHalfFovY. An object around the camera stays. With fadeStartPercent above minScreenPercent,
// an object between the two is kept at a share of itself that grows from 0 at minScreenPercent to 1 at fadeStartPercent
// (storeCullFade), and every other kept object is stored at 1. Before any of that, an object whose box has no side longer
// than minWorldSize yards is culled, wherever it stands, and so is one whose FileDataID is in blocked; a minWorldSize of 0 or
// less culls none, and a minScreenPercent of 0 or less leaves only these culls.
template<typename T>
class ScreenSizeCulling {
public:
    static void cull(const mathfu::vec3 &cameraPos, float tanHalfFovY, float minScreenPercent,
                     const size_t start, const size_t end,
                     const framebased::vector<T> &objects,
                     std::vector<uint32_t> &culling_res,
                     float fadeStartPercent = 0.0f,
                     float minWorldSize = 0.0f,
                     const IdSet *blocked = nullptr) {
        if (blocked != nullptr) {
            std::shared_lock lock(blocked->lock);
            if (!blocked->ids.empty()) {
                for (size_t i = start; i < end; i++) {
                    if (!culling_res[i] && blocked->ids.count(retrieveFileId(objects[i])) > 0) culling_res[i] = 0xFFFFFFFF;
                }
            }
        }
        if (minWorldSize > 0.0f) {
            for (size_t i = start; i < end; i++) {
                if (culling_res[i]) continue;
                const CAaBox &aabb = retrieveAABB(objects[i]);
                const float largestSide = std::max({aabb.max.x - aabb.min.x, aabb.max.y - aabb.min.y, aabb.max.z - aabb.min.z});
                if (largestSide < minWorldSize) culling_res[i] = 0xFFFFFFFF;
            }
        }
        if (minScreenPercent <= 0.0f || tanHalfFovY <= 0.0f) return;
        const bool fading = fadeStartPercent > minScreenPercent;
        // The shader tells this many shares apart, so a finer fade only costs uploads.
        constexpr float FadeSteps = 16.0f;
        for (size_t i = start; i < end; i++) {
            if (culling_res[i]) continue;
            const CAaBox &aabb = retrieveAABB(objects[i]);
            mathfu::vec3 aabbMin = mathfu::vec3(aabb.min.x, aabb.min.y, aabb.min.z);
            mathfu::vec3 aabbMax = mathfu::vec3(aabb.max.x, aabb.max.y, aabb.max.z);
            float radius = std::max((aabbMax - aabbMin).Length() * 0.5f, retrieveScreenRadius(objects[i]));
            float distance = ((aabbMin + aabbMax) * 0.5f - cameraPos).Length();
            if (distance <= radius) {
                if (fading) storeCullFade(objects[i], 1.0f);
                continue;
            }
            const float percent = 100.0f * radius / (distance * tanHalfFovY);
            if (percent < minScreenPercent) {
                culling_res[i] = 0xFFFFFFFF;
                continue;
            }
            if (!fading) continue;

            const float fade = std::floor(std::min((percent - minScreenPercent) / (fadeStartPercent - minScreenPercent), 1.0f) * FadeSteps) / FadeSteps;
            if (fade <= 0.0f) {
                culling_res[i] = 0xFFFFFFFF;
            } else {
                storeCullFade(objects[i], fade);
            }
        }
    }
};

//Scalar twin of AabbArrayCullingSSE (see mathHelper_culling_sse.h). Kept semantically identical:
//VISIBLE when the camera is inside the box or the box intersects at least one frustum.
class AabbArrayCulling {
public:
    static constexpr uint32_t CULLED = 0;
    static constexpr uint32_t VISIBLE = 0xFFFFFFFFu;

    static void cull(const MathHelper::FrustumCullingData &cullingData,
                     const mathfu::vec4 &cameraPos,
                     const CAaBox *boxes,
                     const int count,
                     uint32_t *visibleOut) {
        for (int i = 0; i < count; i++) {
            const CAaBox &box = boxes[i];

            bool visible =
                (cameraPos.x > box.min.x && cameraPos.x < box.max.x &&
                 cameraPos.y > box.min.y && cameraPos.y < box.max.y &&
                 cameraPos.z > box.min.z && cameraPos.z < box.max.z);

            for (auto &frustum : cullingData.frustums) {
                bool culled = false;
                for (auto &plane : frustum.planes) {
                    //p-vertex (corner farthest from the plane) behind the plane => box is outside
                    float distanceToPlane =
                        std::max(box.min.x * plane.x, box.max.x * plane.x) +
                        std::max(box.min.y * plane.y, box.max.y * plane.y) +
                        std::max(box.min.z * plane.z, box.max.z * plane.z) +
                        plane.w;
                    if (distanceToPlane < 0.0f) {
                        culled = true;
                        break;
                    }
                }
                visible = visible || !culled;
            }

            visibleOut[i] = visible ? VISIBLE : CULLED;
        }
    }
};


#endif //AWEBWOWVIEWERCPP_MATHHELPER_CULLING_H
