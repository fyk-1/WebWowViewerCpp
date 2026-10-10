//
// Camera motion vectors for the FSR upscaler
//

#include "MotionVectorsVLK.h"
#include "../../../../gapi/vulkan/descriptorSets/GDescriptorSet.h"
#include "../../../../gapi/vulkan/textures/GTextureVLK.h"
#include "../../../../gapi/interface/FrameContext.h"
#include <algorithm>
#include <array>
#include <cmath>

/*
robots@Impl: The reprojection matrix.

The shader maps a pixel's NDC position to the previous frame with one matrix, previous view-projection times
the inverse of the current one. World coordinates run to tens of thousands of yards, so the product of two
single precision view-projections, one of them inverted, loses the sub-pixel part of a slow camera's motion.
It is built in double precision on the CPU instead, where the large translations cancel before the result is
rounded to single precision.

The depth image is sampled in DEPTH_STENCIL_READ_ONLY_OPTIMAL and handed back in
DEPTH_STENCIL_ATTACHMENT_OPTIMAL, the layouts the Hi-Z step before it and the forward pass after it use. The
outputs are written in GENERAL and left in SHADER_READ_ONLY_OPTIMAL for the upscaler to read.
*/

namespace {
    using Mat4d = std::array<std::array<double, 4>, 4>;

    Mat4d toDouble(const mathfu::mat4 &m) {
        Mat4d result;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                result[r][c] = m(r, c);
        return result;
    }

    Mat4d multiply(const Mat4d &a, const Mat4d &b) {
        Mat4d result = {};
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                for (int k = 0; k < 4; k++)
                    result[r][c] += a[r][k] * b[k][c];
        return result;
    }

    // Gauss-Jordan with partial pivoting. A singular matrix gives false.
    bool invert(const Mat4d &m, Mat4d &out) {
        Mat4d a = m;
        out = {};
        for (int i = 0; i < 4; i++) out[i][i] = 1.0;

        for (int col = 0; col < 4; col++) {
            int pivot = col;
            for (int r = col + 1; r < 4; r++)
                if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
            if (std::fabs(a[pivot][col]) < 1e-300) return false;

            std::swap(a[col], a[pivot]);
            std::swap(out[col], out[pivot]);

            double scale = 1.0 / a[col][col];
            for (int c = 0; c < 4; c++) {
                a[col][c] *= scale;
                out[col][c] *= scale;
            }
            for (int r = 0; r < 4; r++) {
                if (r == col) continue;
                double factor = a[r][col];
                if (factor == 0.0) continue;
                for (int c = 0; c < 4; c++) {
                    a[r][c] -= factor * a[col][c];
                    out[r][c] -= factor * out[col][c];
                }
            }
        }
        return true;
    }

    struct MotionVectorPushConstants {
        mathfu::mat4 reproject;
        float invViewProjRowW[4];
        float nearFarDepthMinDepthRange[4];
        float sizeFarNdcZ[4];
    };
    static_assert(sizeof(MotionVectorPushConstants) == 112, "push constants are read by motionVectors.comp.slang");
}

MotionVectorsVLK::MotionVectorsVLK(const HGDeviceVLK &device) : m_device(device) {
}

void MotionVectorsVLK::setup(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_motionTextures[0] &&
        (int)m_motionTextures[0]->getWidth() == width &&
        (int)m_motionTextures[0]->getHeight() == height) return;

    for (int i = 0; i < IDevice::MAX_FRAMES_IN_FLIGHT; i++) {
        m_motionTextures[i] = std::make_shared<GTextureVLK>(
            *m_device,
            width, height,
            false,
            VK_FORMAT_R16G16_SFLOAT,
            VK_SAMPLE_COUNT_1_BIT,
            1,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
        m_depthTextures[i] = std::make_shared<GTextureVLK>(
            *m_device,
            width, height,
            false,
            VK_FORMAT_R32_SFLOAT,
            VK_SAMPLE_COUNT_1_BIT,
            1,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
        m_descriptorSets[i] = nullptr;
    }

    if (m_material == nullptr) {
        ComputeShaderConfig config;
        config.computeShaderFolder = "compute";
        config.typeOverrides = {
            {0, {
                {1, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false, 1, VK_SHADER_STAGE_COMPUTE_BIT}},
                {2, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false, 1, VK_SHADER_STAGE_COMPUTE_BIT}}
            }}
        };

        m_material = ComputeMaterialBuilderVLK::fromShader(m_device, "motionVectors", config).toMaterial();
    }
}

void MotionVectorsVLK::execute(CmdBufRecorder &cmdBuf,
                               const std::shared_ptr<GFrameBufferVLK> &gBufferFB,
                               const Params &params) {
    if (!isReady() || !gBufferFB) return;

    auto frameNum = FrameContext::getCurrentProcessingFrameNumber() % IDevice::MAX_FRAMES_IN_FLIGHT;

    auto depthTexVlk = std::dynamic_pointer_cast<GTextureVLK>(gBufferFB->getDepthTexture());
    if (!depthTexVlk) return;

    VkImage depthImage = depthTexVlk->texture.image;
    VkImage motionImage = m_motionTextures[frameNum]->texture.image;
    VkImage standardDepthImage = m_depthTextures[frameNum]->texture.image;
    uint32_t width = m_motionTextures[frameNum]->getWidth();
    uint32_t height = m_motionTextures[frameNum]->getHeight();

    // The descriptor set is recreated by every update, which is how the Hi-Z step keeps its sets too.
    if (m_descriptorSets[frameNum] == nullptr) {
        m_descriptorSets[frameNum] = std::make_shared<GDescriptorSet>(m_device, m_material->shader->getDescriptorLayout(0));
    }
    auto depthSource = gBufferFB->getDepthTexture();
    if (m_depthSource[frameNum] != depthSource || m_depthSampled[frameNum] == nullptr) {
        m_depthSource[frameNum] = depthSource;
        m_depthSampled[frameNum] = m_device->createSampledTexture(depthSource, false, false);
    }
    m_descriptorSets[frameNum]->beginUpdate()
        .texture_depth(0, m_depthSampled[frameNum])
        .storage_image(1, m_motionTextures[frameNum]->texture.view)
        .storage_image(2, m_depthTextures[frameNum]->texture.view);

    const VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    // ---- Depth to sampling, outputs to writing ----
    {
        std::vector<VkImageMemoryBarrier> barriers(3);

        barriers[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barriers[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[0].image = depthImage;
        barriers[0].subresourceRange = {depthAspect, 0, 1, 0, 1};

        for (int i = 0; i < 2; i++) {
            barriers[1 + i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barriers[1 + i].srcAccessMask = 0;
            barriers[1 + i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[1 + i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1 + i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[1 + i].image = (i == 0) ? motionImage : standardDepthImage;
            barriers[1 + i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }

        cmdBuf.recordPipelineImageBarrier(
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            barriers
        );
    }

    // ---- Dispatch ----
    {
        Mat4d viewProj = toDouble(params.viewProj);
        Mat4d inverseViewProj;
        Mat4d reproject = {};
        for (int i = 0; i < 4; i++) reproject[i][i] = 1.0;
        if (invert(viewProj, inverseViewProj)) {
            reproject = multiply(toDouble(params.prevViewProj), inverseViewProj);
        } else {
            std::fill(inverseViewProj[3].begin(), inverseViewProj[3].end(), 0.0);
        }

        MotionVectorPushConstants pc;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                pc.reproject(r, c) = (float)reproject[r][c];
        for (int c = 0; c < 4; c++)
            pc.invViewProjRowW[c] = (float)inverseViewProj[3][c];

        // Match the viewport depth range of CmdBufRecorder::createViewPortTypes (vp_usual, not inverted), as the
        // Hi-Z step does. The shader's sky test also takes the end of this range as far.
        const float vpMinDepth = 0.0f;
        const float vpMaxDepth = 0.990f;
        pc.nearFarDepthMinDepthRange[0] = params.nearPlane;
        pc.nearFarDepthMinDepthRange[1] = params.farPlane;
        pc.nearFarDepthMinDepthRange[2] = vpMinDepth;
        pc.nearFarDepthMinDepthRange[3] = vpMaxDepth - vpMinDepth;
        pc.sizeFarNdcZ[0] = (float)width;
        pc.sizeFarNdcZ[1] = (float)height;
        pc.sizeFarNdcZ[2] = params.farNdcZ;
        pc.sizeFarNdcZ[3] = 0.0f;

        std::vector<std::shared_ptr<GDescriptorSet>> descriptorSets = {m_descriptorSets[frameNum]};
        cmdBuf.bindComputePipeline(m_material->pipeline);
        cmdBuf.bindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, descriptorSets);
        cmdBuf.pushConstants(
            m_material->pipeline->getLayout()->getLayout(),
            VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(pc), &pc
        );
        cmdBuf.dispatch((width + 7) / 8, (height + 7) / 8, 1);
    }

    // ---- Depth back to attachment, outputs to reading ----
    {
        std::vector<VkImageMemoryBarrier> barriers(3);

        barriers[0] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barriers[0].image = depthImage;
        barriers[0].subresourceRange = {depthAspect, 0, 1, 0, 1};

        for (int i = 0; i < 2; i++) {
            barriers[1 + i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barriers[1 + i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[1 + i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barriers[1 + i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[1 + i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[1 + i].image = (i == 0) ? motionImage : standardDepthImage;
            barriers[1 + i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }

        cmdBuf.recordPipelineImageBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            barriers
        );
    }
}
