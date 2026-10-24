//
// Flyover: a camera that flies along a spline through a list of world-space points.
//

#include <algorithm>
#include <cmath>
#include "pathCamera.h"

/*
robots@Impl: Spline and arc-length table.

The path is a centripetal Catmull-Rom spline (alpha 0.5) through every point. Centripetal knot spacing
keeps the curve from looping or overshooting where nodes bunch up, which a uniform spline does. The first
and last point are duplicated as end tangents, so the curve starts and ends on them.

The spline parameter is not distance, so flight runs on a lookup instead. At load each segment is sampled in
kSamplesPerSegment steps and the sampled chord lengths are summed into a cumulative table. A distance in
yards maps to a sample pair by binary search and interpolates linearly inside it, and m_length is the
table's total. The look-at point goes through the same lookup lookAhead yards further on, past the end it
extends along the end tangent.

The knot step is clamped, because a duplicated end point or two equal nodes would otherwise divide by zero.
The blend runs in doubles on coordinates relative to the segment's second point: the clamped step makes the
blend weights large, and in float on raw world coordinates (around 10000) that loses whole yards.

The view direction is eased with an exponential of time constant kViewEaseSeconds on top of the spline,
so a tight node never kicks the view. It restarts from the target direction on a loop wrap.
*/

namespace {
    constexpr int kSamplesPerSegment = 32;
    constexpr float kViewEaseSeconds = 0.3f;
    constexpr double kMaxDtMs = 100.0;

    struct D3 {
        double x = 0, y = 0, z = 0;
    };
    inline D3 operator+(const D3 &a, const D3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline D3 operator-(const D3 &a, const D3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline D3 operator*(const D3 &a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    inline double length(const D3 &a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

    inline D3 toD3(const mathfu::vec3 &v) { return {v.x, v.y, v.z}; }

    // Barry-Goldman evaluation of the centripetal Catmull-Rom segment p1..p2 at u in [0, 1].
    D3 catmullRom(const D3 &p0, const D3 &p1, const D3 &p2, const D3 &p3, double u) {
        auto step = [](const D3 &a, const D3 &b) {
            return std::max(std::sqrt(length(b - a)), 1e-3);
        };
        double t0 = 0;
        double t1 = t0 + step(p0, p1);
        double t2 = t1 + step(p1, p2);
        double t3 = t2 + step(p2, p3);
        double t = t1 + u * (t2 - t1);

        D3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
        D3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
        D3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));

        D3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
        D3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));

        return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
    }
}

PathCamera::PathCamera(const std::vector<mathfu::vec3> &points, bool loop, float speedYardsPerSecond,
                       float lookAheadYards) :
    m_loop(loop), m_speed(speedYardsPerSecond), m_lookAhead(lookAheadYards) {
    buildSpline(points);
    updatePose(0.0f, true);
}

void PathCamera::buildSpline(const std::vector<mathfu::vec3> &points) {
    m_samples.clear();
    m_cumulative.clear();
    m_length = 0;

    if (points.empty()) {
        m_samples.push_back({0, 0, 0});
        m_cumulative.push_back(0);
        return;
    }

    const int n = (int) points.size();
    auto controlPoint = [&](int i) {
        return toD3(points[std::clamp(i, 0, n - 1)]);
    };

    m_samples.push_back(points[0]);
    m_cumulative.push_back(0);

    for (int segment = 0; segment + 1 < n; segment++) {
        // Relative to the segment's second point, see the Impl note above.
        D3 origin = controlPoint(segment);
        D3 p0 = controlPoint(segment - 1) - origin;
        D3 p1 = controlPoint(segment) - origin;
        D3 p2 = controlPoint(segment + 1) - origin;
        D3 p3 = controlPoint(segment + 2) - origin;

        for (int k = 1; k <= kSamplesPerSegment; k++) {
            D3 point = catmullRom(p0, p1, p2, p3, (double) k / kSamplesPerSegment) + origin;
            mathfu::vec3 sample((float) point.x, (float) point.y, (float) point.z);
            // The last sample of a segment is the next node, take it exactly.
            if (k == kSamplesPerSegment) {
                sample = points[segment + 1];
            }

            m_cumulative.push_back(m_cumulative.back() + (sample - m_samples.back()).Length());
            m_samples.push_back(sample);
        }
    }

    m_length = m_cumulative.back();

    // End tangent from the last sample pair that has a length.
    for (size_t i = m_samples.size() - 1; i > 0; i--) {
        mathfu::vec3 diff = m_samples[i] - m_samples[i - 1];
        if (diff.Length() > 1e-4f) {
            m_endTangent = diff.Normalized();
            break;
        }
    }
}

mathfu::vec3 PathCamera::pointAt(float distance) const {
    if (m_samples.size() < 2 || distance <= 0) return m_samples.front();
    if (distance >= m_length) return m_samples.back();

    // First sample whose cumulative length exceeds distance.
    size_t hi = std::upper_bound(m_cumulative.begin(), m_cumulative.end(), distance) - m_cumulative.begin();
    if (hi == 0) return m_samples.front();
    if (hi >= m_samples.size()) return m_samples.back();

    size_t lo = hi - 1;
    float span = m_cumulative[hi] - m_cumulative[lo];
    float frac = span > 1e-9f ? (distance - m_cumulative[lo]) / span : 0.0f;
    return m_samples[lo] + (m_samples[hi] - m_samples[lo]) * frac;
}

mathfu::vec3 PathCamera::pointAhead(float distance) const {
    if (distance <= m_length) return pointAt(distance);

    return m_samples.back() + m_endTangent * (distance - m_length);
}

void PathCamera::setDistance(float distance) {
    m_distance = std::clamp(distance, 0.0f, m_length);
    updatePose(0.0f, true);
}

void PathCamera::seedViewDirection(const mathfu::vec3 &direction) {
    if (direction.Length() < 1e-6f) return;

    m_viewDirection = direction.Normalized();
    m_viewDirectionValid = true;
}

void PathCamera::tick(animTime_t timeDelta) {
    // A paused or resumed process hands in one huge delta, which would jump the flight.
    double dtSeconds = std::clamp((double) timeDelta, 0.0, kMaxDtMs) / 1000.0;

    bool wrapped = false;
    if (!m_paused && m_length > 0) {
        float previous = m_distance;
        float next = previous + (float) dtSeconds * m_speed;

        if (m_loop) {
            next = std::fmod(next, m_length);
            if (next < 0) next += m_length;
            wrapped = (m_speed >= 0) ? (next < previous) : (next > previous);
        } else {
            next = std::clamp(next, 0.0f, m_length);
        }
        m_distance = next;
    }

    updatePose((float) dtSeconds, wrapped);
}

void PathCamera::updatePose(float dtSeconds, bool snapView) {
    mathfu::vec3 onLine = pointAt(m_distance);
    m_position = onLine + mathfu::vec3(0, 0, m_eye);

    mathfu::vec3 toLook = pointAhead(m_distance + m_lookAhead) - onLine;
    if (toLook.Length() > 1e-4f) {
        mathfu::vec3 target = toLook.Normalized();
        if (!m_viewDirectionValid || snapView) {
            m_viewDirection = target;
            m_viewDirectionValid = true;
        } else {
            float alpha = 1.0f - std::exp(-dtSeconds / kViewEaseSeconds);
            mathfu::vec3 eased = m_viewDirection + (target - m_viewDirection) * alpha;
            if (eased.Length() > 1e-6f) {
                m_viewDirection = eased.Normalized();
            }
        }
    }

    // Bank stays 0, up is +Z as in FirstPersonCamera. Turn rate is the later source of bank.
    mathfu::vec3 up = {0, 0, 1};

    m_camera = mathfu::vec4(m_position, 1.0f);
    m_lookAt = m_position + m_viewDirection;

    m_lookAtMat = mathfu::mat4::LookAt(m_lookAt, m_position, up, 1);
    m_invTranspViewMat = m_lookAtMat.Inverse().Transpose();

    mathfu::vec4 interiorSunDir = mathfu::vec4(-0.30822f, -0.30822f, -0.89999998f, 0);
    interiorSunDir = m_invTranspViewMat * interiorSunDir;
    interiorSunDir = mathfu::vec4(interiorSunDir.xyz() * (1.0f / interiorSunDir.xyz().Length()), 0.0f);
    m_interiorDirectLightDir = interiorSunDir;

    mathfu::vec4 upVector(0.0, 0.0, 1.0, 0.0);
    m_upVector = ((m_invTranspViewMat * upVector).xyz()).Normalized();

    m_updatedAtLeastOnce = true;
}

bool PathCamera::getPreloadPoint(float yardsAhead, mathfu::vec3 &point) {
    if (m_samples.size() < 2 || m_length <= 0) return false;

    float distance = m_distance + yardsAhead;
    if (m_loop) {
        distance = std::fmod(distance, m_length);
        if (distance < 0) distance += m_length;
    }

    point = pointAt(distance);
    return true;
}

HCameraMatrices PathCamera::getCameraMatrices(float fov,
                                              float canvasAspect,
                                              float nearPlane,
                                              float farPlane) {
    if (!m_updatedAtLeastOnce)
        updatePose(0.0f, true);

    HCameraMatrices cameraMatrices = std::make_shared<CameraMatrices>();
    cameraMatrices->perspectiveMat = mathfu::mat4::Perspective(fov, canvasAspect, nearPlane, farPlane, 1.0f);
    cameraMatrices->lookAtMat = m_lookAtMat;
    cameraMatrices->invTranspViewMat = m_invTranspViewMat;

    cameraMatrices->cameraPos = m_camera;
    cameraMatrices->lookAt = mathfu::vec4(m_lookAt, 1.0f);
    cameraMatrices->viewUp = mathfu::vec4(m_upVector, 0);
    cameraMatrices->interiorDirectLightDir = m_interiorDirectLightDir;

    return cameraMatrices;
}
