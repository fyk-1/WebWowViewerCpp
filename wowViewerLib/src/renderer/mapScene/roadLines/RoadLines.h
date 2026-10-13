//
// The road recorder's ghost line: the baked roads around the camera and the running take, as thin lines over the world.
//
// The host hands the viewer the roads of the zones around the camera and the points of the take (`roadlines` and `take` on stdin, see
// src/flyover/FlyoverMode.cpp). The main thread stores them here, and the map renderer reads the segments near the camera each frame and
// draws them in its weather pass (MapSceneRenderBindlessVLK.cpp).
//

#ifndef AWEBWOWVIEWERCPP_ROADLINES_H
#define AWEBWOWVIEWERCPP_ROADLINES_H

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>
#include "mathfu/glsl_mappings.h"

// robots@Impl: A road is stored as segments with its lift already added, and a take as its points. The renderer takes the road segments
// with an end within `radius` yards of the camera over the ground, which keeps a zone's whole road set cheap to hold and the draw to
// what is near, and the take's last TakeSegments segments, wherever they lie. The segments of both together never pass MaxSegments, the
// size of the renderer's buffer; roads give way to the take. A road lies RoadLift yards over the ground it was baked at, so a slope
// between two baked points does not cut the line into the terrain; a take is drawn where the camera was.
class RoadLines {
public:
    static constexpr int MaxSegments = 24576;
    static constexpr int TakeSegments = 8192;
    static constexpr float RoadLift = 0.75f;

    struct Segment {
        mathfu::vec3 from;
        mathfu::vec3 to;
        // 0 for a road, 1 for the take.
        float kind;
    };

    // Replaces the roads: each line is a polyline of world points.
    void setRoads(const std::vector<std::vector<mathfu::vec3>> &lines) {
        std::vector<Segment> segments;
        for (const auto &line : lines) {
            for (size_t i = 1; i < line.size(); i++) {
                const mathfu::vec3 lift(0, 0, RoadLift);
                segments.push_back({line[i - 1] + lift, line[i] + lift, 0.0f});
            }
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_roads = std::move(segments);
    }

    void clearRoads() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_roads.clear();
    }

    void clearTake() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_take.clear();
    }

    void addTakePoint(const mathfu::vec3 &point) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_take.push_back(point);
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_roads.empty() && m_take.size() < 2;
    }

    // Replaces `out` with the segments to draw for a camera at `center`: the road segments within `radius` yards over the ground, then the take.
    void gather(const mathfu::vec3 &center, float radius, std::vector<Segment> &out) const {
        out.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t takeCount = m_take.size() < 2 ? 0 : std::min<size_t>(m_take.size() - 1, TakeSegments);
        const size_t roadRoom = MaxSegments - takeCount;
        const float reach = radius * radius;
        auto within = [&](const mathfu::vec3 &p) {
            const float dx = p.x - center.x, dy = p.y - center.y;
            return dx * dx + dy * dy <= reach;
        };
        for (const auto &segment : m_roads) {
            if (out.size() >= roadRoom) break;
            if (within(segment.from) || within(segment.to)) out.push_back(segment);
        }
        for (size_t i = m_take.size() - takeCount; takeCount > 0 && i < m_take.size(); i++) {
            out.push_back({m_take[i - 1], m_take[i], 1.0f});
        }
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Segment> m_roads;
    std::vector<mathfu::vec3> m_take;
};

#endif //AWEBWOWVIEWERCPP_ROADLINES_H
