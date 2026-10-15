#include "bakeCamera.h"

#include "../algorithms/mathHelper.h"
#include "../../gapi/vulkan/commandBuffer/commandBufferRecorder/CommandBufferRecorder.h"

void BakeCamera::place(float x, float y, float half, float top, float bottom) {
    m_x = x;
    m_y = y;
    m_half = half;
    m_top = top;
    m_bottom = bottom;
}

mathfu::mat4 BakeCamera::projection() const {
    // Distances along the view axis, from the camera at the top of the volume.
    return mathfu::mat4::Ortho(-m_half, m_half, -m_half, m_half, 0.0f, m_top - m_bottom, 1.0f);
}

float BakeCamera::zOfDepth(float depth) const {
    // The scene draws through the usual viewport, so the depth the buffer holds is the clip depth scaled to its range.
    const float clipDepth = depth / CmdBufRecorder::kUsualViewportMaxDepth;
    const mathfu::mat4 full = MathHelper::getVulkanMat4Fix() * projection();
    // An orthographic clip w is 1, so the clip depth is linear in the view-space z: full(2, 2) * z + full(2, 3).
    const float viewZ = (clipDepth - full(2, 3)) / full(2, 2);
    return m_top + viewZ;
}

HCameraMatrices BakeCamera::getCameraMatrices(float fov, float canvasAspect, float nearPlane, float farPlane) {
    auto matrices = std::make_shared<CameraMatrices>();
    const mathfu::vec3 position(m_x, m_y, m_top);

    // Rows: right (-y), up (+x), back (+z), then the translation that takes the camera to the origin.
    const mathfu::mat4 rotation(
        0, 1, 0, 0,
        -1, 0, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
    matrices->perspectiveMat = projection();
    matrices->lookAtMat = rotation * mathfu::mat4::FromTranslationVector(-position);
    matrices->invTranspViewMat = matrices->lookAtMat.Inverse().Transpose();
    matrices->cameraPos = mathfu::vec4(position, 1.0f);
    matrices->lookAt = mathfu::vec4(position + mathfu::vec3(0, 0, -1), 1.0f);
    matrices->viewUp = mathfu::vec4(1, 0, 0, 0);
    matrices->interiorDirectLightDir = mathfu::vec4(0, 0, -1, 0);
    return matrices;
}
