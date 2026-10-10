//
// Created by Deamon on 29.09.22.
//

#ifndef AWEBWOWVIEWERCPP_MATHHELPER_CULLING_H
#define AWEBWOWVIEWERCPP_MATHHELPER_CULLING_H

#include <array>
#include "mathHelper.h"

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
// seen from cameraPos with tanHalfFovY. An object around the camera stays.
template<typename T>
class ScreenSizeCulling {
public:
    static void cull(const mathfu::vec3 &cameraPos, float tanHalfFovY, float minScreenPercent,
                     const size_t start, const size_t end,
                     const framebased::vector<T> &objects,
                     std::vector<uint32_t> &culling_res) {
        if (minScreenPercent <= 0.0f || tanHalfFovY <= 0.0f) return;
        for (size_t i = start; i < end; i++) {
            if (culling_res[i]) continue;
            const CAaBox &aabb = retrieveAABB(objects[i]);
            mathfu::vec3 aabbMin = mathfu::vec3(aabb.min.x, aabb.min.y, aabb.min.z);
            mathfu::vec3 aabbMax = mathfu::vec3(aabb.max.x, aabb.max.y, aabb.max.z);
            float radius = std::max((aabbMax - aabbMin).Length() * 0.5f, retrieveScreenRadius(objects[i]));
            float distance = ((aabbMin + aabbMax) * 0.5f - cameraPos).Length();
            if (distance <= radius) continue;
            if (100.0f * radius / (distance * tanHalfFovY) < minScreenPercent) culling_res[i] = 0xFFFFFFFF;
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
