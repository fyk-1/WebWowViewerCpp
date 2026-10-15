#ifndef WOWVIEWERLIB_BAKECAMERA_H
#define WOWVIEWERLIB_BAKECAMERA_H

#include <mathfu/glsl_mappings.h>
#include "CameraInterface.h"

/*
 * The camera of a height bake: an orthographic view straight down over a square of the ground, which a depth-only render turns into
 * heights (src/flyover/HeightBake.cpp). It ignores the fov, aspect and planes the scene window hands every camera.
 *
 * The view looks along -z with +x up the image and -y to its right, so image row 0 is the highest world x and image column 0 the
 * highest world y, the frame of FarHeights. The square spans `half` yards either side of (x, y), and the volume runs from `top`
 * down to `bottom` in z.
 */
class BakeCamera : public ICamera {
public:
    void place(float x, float y, float half, float top, float bottom);

    // The world z of a depth the scene's depth buffer holds for a fragment of the bake, inverting the whole chain: this camera's
    // projection, the Vulkan fix every projection is multiplied by, and the usual viewport's depth range.
    float zOfDepth(float depth) const;

    HCameraMatrices getCameraMatrices(float fov, float canvasAspect, float nearPlane, float farPlane) override;
    void tick(animTime_t timeDelta) override {}
    float getMovementSpeed() override { return 0.0f; }
    void setMovementSpeed(float value) override {}

    void startMovingForward() override {}
    void stopMovingForward() override {}
    void startMovingBackwards() override {}
    void stopMovingBackwards() override {}
    void startStrafingLeft() override {}
    void stopStrafingLeft() override {}
    void startStrafingRight() override {}
    void stopStrafingRight() override {}
    void startMovingUp() override {}
    void stopMovingUp() override {}
    void startMovingDown() override {}
    void stopMovingDown() override {}
    void stopAllMovement() override {}
    void addForwardDiff(float val) override {}
    void addHorizontalViewDir(float val) override {}
    void addVerticalViewDir(float val) override {}
    void zoomInFromMouseScroll(float val) override {}
    void zoomInFromTouch(float val) override {}
    void addCameraViewOffset(float x, float y) override {}
    void setCameraPos(float x, float y, float z) override {}
    void getCameraPosition(float *position) override {
        position[0] = m_x;
        position[1] = m_y;
        position[2] = m_top;
    }
    void setCameraLookAt(float x, float y, float z) override {}

    float centerX() const { return m_x; }
    float centerY() const { return m_y; }
    float top() const { return m_top; }

private:
    mathfu::mat4 projection() const;

    float m_x = 0, m_y = 0, m_half = 1, m_top = 1, m_bottom = 0;
};

#endif //WOWVIEWERLIB_BAKECAMERA_H
