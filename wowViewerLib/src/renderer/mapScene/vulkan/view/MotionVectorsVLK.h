//
// Camera motion vectors for the FSR upscaler
//

#ifndef AWEBWOWVIEWERCPP_MOTIONVECTORSVLK_H
#define AWEBWOWVIEWERCPP_MOTIONVECTORSVLK_H

#include "../../../../gapi/vulkan/GDeviceVulkan.h"
#include "../../../../gapi/vulkan/materials/ComputeMaterialBuilderVLK.h"
#include "../../../../gapi/vulkan/textures/GTextureVLK.h"
#include "../../../../gapi/vulkan/GFrameBufferVLK.h"
#include "../../../../gapi/vulkan/commandBuffer/commandBufferRecorder/CommandBufferRecorder.h"

class MotionVectorsVLK {
public:
    struct Params {
        // The unjittered view-projection of this frame and of the previous one.
        mathfu::mat4 viewProj;
        mathfu::mat4 prevViewProj;
        float nearPlane;
        float farPlane;
        // NDC depth of the far plane under viewProj, given to pixels the geometry never wrote.
        float farNdcZ;
    };

    explicit MotionVectorsVLK(const HGDeviceVLK &device);

    // Recreates the textures and the pipeline when the render size changes.
    void setup(int width, int height);

    // Writes the motion vectors and the standard depth from the GBuffer depth, which has to be in
    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL and is left there, and has to run the usual direction (1 is far). Both outputs end in SHADER_READ_ONLY_OPTIMAL.
    void execute(CmdBufRecorder &cmdBuf, const std::shared_ptr<GFrameBufferVLK> &gBufferFB, const Params &params);

    bool isReady() const { return m_material != nullptr; }

    // RG16F, previous UV minus current UV
    const std::shared_ptr<GTextureVLK> &getMotionTexture(int frameNum) const { return m_motionTextures[frameNum]; }
    // R32F, standard device depth of the camera's near and far
    const std::shared_ptr<GTextureVLK> &getDepthTexture(int frameNum) const { return m_depthTextures[frameNum]; }

private:
    HGDeviceVLK m_device;

    std::array<std::shared_ptr<GTextureVLK>, IDevice::MAX_FRAMES_IN_FLIGHT> m_motionTextures;
    std::array<std::shared_ptr<GTextureVLK>, IDevice::MAX_FRAMES_IN_FLIGHT> m_depthTextures;
    std::array<std::shared_ptr<GDescriptorSet>, IDevice::MAX_FRAMES_IN_FLIGHT> m_descriptorSets;
    // The GBuffer depth as sampled, remade only when the framebuffer hands out another depth texture
    std::array<HGTexture, IDevice::MAX_FRAMES_IN_FLIGHT> m_depthSource;
    std::array<HGSamplableTexture, IDevice::MAX_FRAMES_IN_FLIGHT> m_depthSampled;

    std::shared_ptr<ComputeMaterialVLK> m_material;
};

#endif //AWEBWOWVIEWERCPP_MOTIONVECTORSVLK_H
