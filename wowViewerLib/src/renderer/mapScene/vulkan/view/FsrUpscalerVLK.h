//
// AMD FidelityFX Super Resolution 3.1 upscaler, through the ffx API and its Vulkan DLL
//

#ifndef AWEBWOWVIEWERCPP_FSRUPSCALERVLK_H
#define AWEBWOWVIEWERCPP_FSRUPSCALERVLK_H

#include <algorithm>
#include <array>
#include <memory>
#include "../../../../gapi/vulkan/GDeviceVulkan.h"
#include "../../../../gapi/vulkan/textures/GTextureVLK.h"
#include "../../../../gapi/vulkan/commandBuffer/commandBufferRecorder/CommandBufferRecorder.h"

class FsrUpscalerVLK {
public:
    struct DispatchParams {
        // Subpixel offset of the projection this frame was rendered with, in render pixels
        float jitterX;
        float jitterY;
        float frameTimeMs;
        float cameraNear;
        float cameraFar;
        float cameraFovY;
        // RCAS sharpening, 0 to 1
        float sharpness;
        // The camera jumped; the upscaler drops its history
        bool reset;
    };

    // The size FSR upscales to for a window, at outputScale of it (held to 0.25 to 1).
    static VkExtent2D outputExtent(VkExtent2D window, float outputScale) {
        float scale = std::clamp(outputScale, 0.25f, 1.0f);
        return {std::max(1u, (uint32_t) (window.width * scale)), std::max(1u, (uint32_t) (window.height * scale))};
    }

    // True while the FidelityFX DLL is loaded and no use of it has failed. Loads the DLL on the first call.
    static bool isAvailable();
    // The jitter sequence is a property of the DLL and needs no context.
    static int32_t getJitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth);
    static bool getJitterOffset(int32_t index, int32_t phaseCount, float &offsetX, float &offsetY);

    explicit FsrUpscalerVLK(const HGDeviceVLK &device);
    ~FsrUpscalerVLK();

    // Makes the context and its textures match the sizes, recreating them when they differ. A fresh context
    // resets the upscaler's history on its first dispatch. False when FSR can't be used; the reason went to stderr.
    bool ensureContext(uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight);
    void releaseContext();
    bool hasContext() const { return m_context != nullptr; }
    bool matchesSizes(uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight) const {
        return m_context != nullptr && m_renderWidth == renderWidth && m_renderHeight == renderHeight &&
               m_displayWidth == displayWidth && m_displayHeight == displayHeight;
    }
    // Counts the contexts made, so a holder of the output textures can tell they were recreated
    uint32_t getContextGeneration() const { return m_generation; }

    // Render size copy of the opaque color, taken after the first forward pass, that the reactive mask is made against
    const std::shared_ptr<GTextureVLK> &getOpaqueColorTexture(int frameNum) const { return m_opaqueColor[frameNum]; }
    // Display size result, in SHADER_READ_ONLY_OPTIMAL after a dispatch
    HGSamplableTexture getOutputSamplable(int frameNum) const { return m_outputSamplable[frameNum]; }

    // Builds the reactive mask from the opaque copy and the final color, then upscales. Every input is expected in
    // SHADER_READ_ONLY_OPTIMAL and visible to compute. False when a dispatch failed.
    bool dispatch(CmdBufRecorder &cmdBuf,
                  const std::shared_ptr<GTextureVLK> &color,
                  const std::shared_ptr<GTextureVLK> &depth,
                  const std::shared_ptr<GTextureVLK> &motionVectors,
                  const DispatchParams &params);

private:
    HGDeviceVLK m_device;

    void *m_context = nullptr;
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_displayWidth = 0;
    uint32_t m_displayHeight = 0;
    bool m_resetPending = false;
    uint32_t m_generation = 0;

    std::array<std::shared_ptr<GTextureVLK>, IDevice::MAX_FRAMES_IN_FLIGHT> m_opaqueColor;
    std::array<std::shared_ptr<GTextureVLK>, IDevice::MAX_FRAMES_IN_FLIGHT> m_reactive;
    std::array<std::shared_ptr<GTextureVLK>, IDevice::MAX_FRAMES_IN_FLIGHT> m_output;
    std::array<HGSamplableTexture, IDevice::MAX_FRAMES_IN_FLIGHT> m_outputSamplable;
};

#endif //AWEBWOWVIEWERCPP_FSRUPSCALERVLK_H
