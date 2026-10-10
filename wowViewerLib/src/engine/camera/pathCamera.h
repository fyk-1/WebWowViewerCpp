//
// Flyover: a camera that flies along a spline through a list of world-space points.
//

#ifndef WOWVIEWERLIB_PATHCAMERA_H
#define WOWVIEWERLIB_PATHCAMERA_H

#include <vector>
#include <mathfu/vector.h>
#include "mathfu/glsl_mappings.h"
#include "CameraInterface.h"

class PathCamera : public ICamera {
public:
    // points are raw WoW world coordinates. loop wraps the flight at the end of the path, otherwise it stops there.
    PathCamera(const std::vector<mathfu::vec3> &points, bool loop, float speedYardsPerSecond, float lookAheadYards);

    //Path state
    float getDistance() const { return m_distance; }
    void setDistance(float distance);
    float getLengthYards() const { return m_length; }
    void setPaused(bool paused) { m_paused = paused; }
    void setLookAhead(float yards) { m_lookAhead = yards; }
    void setLoop(bool loop) { m_loop = loop; }
    bool isPaused() const { return m_paused; }
    // Starts the eased view direction from the given one, so a switch from another camera does not snap.
    void seedViewDirection(const mathfu::vec3 &direction);

public:
    //Implemented ICamera
    HCameraMatrices getCameraMatrices(float fov,
                                      float canvasAspect,
                                      float nearPlane,
                                      float farPlane) override;
    // timeDelta is in milliseconds, as for every camera.
    void tick(animTime_t timeDelta) override;
    float getMovementSpeed() override { return m_speed; }
    void setMovementSpeed(float value) override { m_speed = value; }
    bool isCompatibleWithInfiniteZ() override { return true; }
    // The spline point yardsAhead past the current distance, wrapping at the end of a looping route.
    bool getPreloadPoint(float yardsAhead, mathfu::vec3 &point) override;

public:
    //Implemented IControllable. The camera ignores all input.
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
    void setCameraLookAt(float x, float y, float z) override {}
    void getCameraPosition(float *position) override {
        position[0] = m_position.x;
        position[1] = m_position.y;
        position[2] = m_position.z;
    }

private:
    void buildSpline(const std::vector<mathfu::vec3> &points);
    mathfu::vec3 pointAt(float distance) const;
    mathfu::vec3 pointAhead(float distance) const;
    void updatePose(float dtSeconds, bool snapView);

    //Arc-length table: sampled spline points and the cumulative length at each.
    std::vector<mathfu::vec3> m_samples;
    std::vector<float> m_cumulative;
    mathfu::vec3 m_endTangent = {1, 0, 0};
    float m_length = 0;

    bool m_loop;
    float m_speed;
    float m_lookAhead;
    bool m_paused = false;

    float m_distance = 0;
    mathfu::vec3 m_position = {0, 0, 0};
    mathfu::vec3 m_viewDirection = {1, 0, 0};
    bool m_viewDirectionValid = false;
    bool m_updatedAtLeastOnce = false;

    mathfu::vec4 m_camera = {0, 0, 0, 1};
    mathfu::vec3 m_lookAt = {0, 0, 0};
    mathfu::vec3 m_upVector = {0, 0, 1};
    mathfu::vec4 m_interiorDirectLightDir = {0, 0, 0, 0};
    mathfu::mat4 m_lookAtMat = {};
    mathfu::mat4 m_invTranspViewMat = {};
};

#endif //WOWVIEWERLIB_PATHCAMERA_H
