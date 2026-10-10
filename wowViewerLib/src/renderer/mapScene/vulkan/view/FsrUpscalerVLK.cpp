//
// AMD FidelityFX Super Resolution 3.1 upscaler, through the ffx API and its Vulkan DLL
//

#include "FsrUpscalerVLK.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_types.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#include <ffx_api/ffx_api_loader.h>
#endif

#include "../../../../gapi/interface/FrameContext.h"

/*
robots@Impl: Loading, failing and the ffx resource states.

The DLL is loaded with LoadLibrary at the first use and its five entry points resolved by ffx_api_loader.h,
rather than linked through the import library, so a missing DLL costs the upscaler and not the process. Any
failure, in loading it, creating the context or dispatching, writes one line starting `fsr:` to stderr and
turns the upscaler off for the rest of the run. The caller then renders as it does without FSR.

The ffx Vulkan backend barriers each resource from the state a dispatch declares to the one it needs. This
class puts every image in the layout that declared state means before the dispatch, so those barriers change
nothing: inputs in SHADER_READ_ONLY_OPTIMAL declared as compute read, outputs in GENERAL declared as
unordered access. The images are written in full each frame, so outputs come from UNDEFINED.

The reactive mask is made from the opaque-only copy and the final color. The constants are the FSR sample's
for LDR input: a binary mask with 0.9 where the two differ by more than 0.2.

The jitter queries need no context, which is how the camera can be jittered a frame before the context exists.
*/

namespace {
    enum class LibraryState { Untried, Loaded, Failed };

    std::mutex g_libraryMutex;
    std::atomic<LibraryState> g_libraryState{LibraryState::Untried};

#ifdef _WIN32
    ffxFunctions g_ffx = {};
#endif

    void failFsr(const std::string &reason) {
        // Only the first failure is the cause; the rest follow from it.
        if (g_libraryState.exchange(LibraryState::Failed) != LibraryState::Failed) {
            std::cerr << "fsr: " << reason << " - rendering without FSR" << std::endl;
        }
    }

#ifdef _WIN32
    void ffxMessage(uint32_t type, const wchar_t *message) {
        std::string text;
        for (const wchar_t *c = message; c != nullptr && *c != 0; c++)
            text.push_back(*c < 128 ? (char) *c : '?');
        std::cerr << "fsr: " << (type == FFX_API_MESSAGE_TYPE_ERROR ? "error: " : "warning: ") << text << std::endl;
    }

    FfxApiResource describeResource(const std::shared_ptr<GTextureVLK> &texture, uint32_t format, uint32_t usage, uint32_t state) {
        FfxApiResource resource = {};
        resource.resource = (void *) texture->texture.image;
        resource.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
        resource.description.format = format;
        resource.description.width = texture->getWidth();
        resource.description.height = texture->getHeight();
        resource.description.depth = 1;
        resource.description.mipCount = 1;
        resource.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
        resource.description.usage = usage;
        resource.state = state;
        return resource;
    }
#endif

    VkImageMemoryBarrier imageBarrier(VkImage image,
                                      VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                      VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return barrier;
    }
}

bool FsrUpscalerVLK::isAvailable() {
    auto state = g_libraryState.load();
    if (state != LibraryState::Untried) return state == LibraryState::Loaded;

    std::lock_guard<std::mutex> lock(g_libraryMutex);
    if (g_libraryState.load() != LibraryState::Untried) return g_libraryState.load() == LibraryState::Loaded;

#ifdef _WIN32
    HMODULE module = LoadLibraryA("amd_fidelityfx_vk.dll");
    if (module == nullptr) {
        failFsr("amd_fidelityfx_vk.dll could not be loaded, error " + std::to_string(GetLastError()));
        return false;
    }

    ffxLoadFunctions(&g_ffx, module);
    if (!g_ffx.CreateContext || !g_ffx.DestroyContext || !g_ffx.Configure || !g_ffx.Query || !g_ffx.Dispatch) {
        failFsr("amd_fidelityfx_vk.dll lacks an ffx entry point");
        return false;
    }

    g_libraryState = LibraryState::Loaded;
    return true;
#else
    failFsr("FSR is only built for Windows");
    return false;
#endif
}

int32_t FsrUpscalerVLK::getJitterPhaseCount(uint32_t renderWidth, uint32_t displayWidth) {
#ifdef _WIN32
    if (!isAvailable()) return 0;

    int32_t phaseCount = 0;
    ffxQueryDescUpscaleGetJitterPhaseCount query = {};
    query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    query.renderWidth = renderWidth;
    query.displayWidth = displayWidth;
    query.pOutPhaseCount = &phaseCount;
    if (g_ffx.Query(nullptr, &query.header) != FFX_API_RETURN_OK) {
        failFsr("the jitter phase count query failed");
        return 0;
    }
    return phaseCount;
#else
    return 0;
#endif
}

bool FsrUpscalerVLK::getJitterOffset(int32_t index, int32_t phaseCount, float &offsetX, float &offsetY) {
#ifdef _WIN32
    if (!isAvailable() || phaseCount <= 0) return false;

    ffxQueryDescUpscaleGetJitterOffset query = {};
    query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    query.index = index;
    query.phaseCount = phaseCount;
    query.pOutX = &offsetX;
    query.pOutY = &offsetY;
    if (g_ffx.Query(nullptr, &query.header) != FFX_API_RETURN_OK) {
        failFsr("the jitter offset query failed");
        return false;
    }
    return true;
#else
    return false;
#endif
}

FsrUpscalerVLK::FsrUpscalerVLK(const HGDeviceVLK &device) : m_device(device) {
}

FsrUpscalerVLK::~FsrUpscalerVLK() {
    releaseContext();
}

void FsrUpscalerVLK::releaseContext() {
    // The context owns the history textures that frames in flight may still be reading, so it dies with the
    // device's deferred deallocations, like the renderer's own textures do.
#ifdef _WIN32
    if (m_context != nullptr) {
        auto context = m_context;
        auto destroyContext = g_ffx.DestroyContext;
        m_device->addDeallocationRecord([context, destroyContext]() mutable {
            ffxContext ffxContextHandle = context;
            destroyContext(&ffxContextHandle, nullptr);
        });
    }
#endif
    m_context = nullptr;

    for (int i = 0; i < IDevice::MAX_FRAMES_IN_FLIGHT; i++) {
        m_opaqueColor[i] = nullptr;
        m_reactive[i] = nullptr;
        m_output[i] = nullptr;
        m_outputSamplable[i] = nullptr;
    }
    m_renderWidth = m_renderHeight = m_displayWidth = m_displayHeight = 0;
}

bool FsrUpscalerVLK::ensureContext(uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight) {
    if (!isAvailable()) return false;

#ifdef _WIN32
    renderWidth = std::max(1u, renderWidth);
    renderHeight = std::max(1u, renderHeight);
    displayWidth = std::max(1u, displayWidth);
    displayHeight = std::max(1u, displayHeight);

    if (matchesSizes(renderWidth, renderHeight, displayWidth, displayHeight)) return true;

    releaseContext();

    // The reactive mask and the motion vectors are storage images in formats Vulkan doesn't require for storage.
    for (VkFormat format : {VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16_SFLOAT}) {
        VkFormatProperties properties = {};
        vkGetPhysicalDeviceFormatProperties(m_device->getVkPhysicalDevice(), format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0) {
            failFsr("the device can't write format " + std::to_string((int) format) + " as a storage image");
            return false;
        }
    }

    ffxCreateBackendVKDesc backendDesc = {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backendDesc.vkDevice = m_device->getVkDevice();
    backendDesc.vkPhysicalDevice = m_device->getVkPhysicalDevice();
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    // The input is the display-ready, gamma-encoded composite, so the only flag is the non-linear colour space;
    // depth runs the usual direction and the motion vectors are at render size without the jitter. Debug builds
    // also have the DLL check the inputs and report through ffxMessage.
    ffxCreateContextDescUpscale createDesc = {};
    createDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    createDesc.header.pNext = &backendDesc.header;
    createDesc.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
#ifndef NDEBUG
    createDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#endif
    createDesc.maxRenderSize = {renderWidth, renderHeight};
    createDesc.maxUpscaleSize = {displayWidth, displayHeight};
    createDesc.fpMessage = ffxMessage;

    ffxContext context = nullptr;
    auto result = g_ffx.CreateContext(&context, &createDesc.header, nullptr);
    if (result != FFX_API_RETURN_OK || context == nullptr) {
        failFsr("context creation failed with code " + std::to_string(result));
        return false;
    }

    m_context = context;
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
    m_resetPending = true;
    m_generation++;

    auto linearSampler = m_device->getSampler(false, false, false);
    for (int i = 0; i < IDevice::MAX_FRAMES_IN_FLIGHT; i++) {
        m_opaqueColor[i] = std::make_shared<GTextureVLK>(
            *m_device,
            renderWidth, renderHeight,
            false,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLE_COUNT_1_BIT,
            1,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
        m_reactive[i] = std::make_shared<GTextureVLK>(
            *m_device,
            renderWidth, renderHeight,
            false,
            VK_FORMAT_R8_UNORM,
            VK_SAMPLE_COUNT_1_BIT,
            1,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
        m_output[i] = std::make_shared<GTextureVLK>(
            *m_device,
            displayWidth, displayHeight,
            false,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_SAMPLE_COUNT_1_BIT,
            1,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        );
        m_outputSamplable[i] = std::make_shared<ISamplableTexture>(m_output[i], linearSampler);
    }
    return true;
#else
    return false;
#endif
}

bool FsrUpscalerVLK::dispatch(CmdBufRecorder &cmdBuf,
                              const std::shared_ptr<GTextureVLK> &color,
                              const std::shared_ptr<GTextureVLK> &depth,
                              const std::shared_ptr<GTextureVLK> &motionVectors,
                              const DispatchParams &params) {
#ifdef _WIN32
    if (m_context == nullptr || !isAvailable()) return false;

    const int frameNum = FrameContext::getCurrentProcessingFrameNumber() % IDevice::MAX_FRAMES_IN_FLIGHT;
    auto &opaqueColor = m_opaqueColor[frameNum];
    auto &reactive = m_reactive[frameNum];
    auto &output = m_output[frameNum];

    // The final color was drawn as an attachment, and is read here by compute.
    // The reactive mask is written whole, in GENERAL.
    cmdBuf.recordPipelineImageBarrier(
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {
            imageBarrier(color->texture.image,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            imageBarrier(reactive->texture.image,
                         0, VK_ACCESS_SHADER_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        }
    );

    const uint32_t colorFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    const uint32_t reactiveFormat = FFX_API_SURFACE_FORMAT_R8_UNORM;
    const FfxApiDimensions2D renderSize = {m_renderWidth, m_renderHeight};
    const FfxApiDimensions2D displaySize = {m_displayWidth, m_displayHeight};
    VkCommandBuffer nativeCmd = cmdBuf.getNativeCmdBuffer();

    ffxContext context = m_context;

    ffxDispatchDescUpscaleGenerateReactiveMask reactiveDesc = {};
    reactiveDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK;
    reactiveDesc.commandList = nativeCmd;
    reactiveDesc.colorOpaqueOnly = describeResource(opaqueColor, colorFormat, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    reactiveDesc.colorPreUpscale = describeResource(color, colorFormat, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    reactiveDesc.outReactive = describeResource(reactive, reactiveFormat, FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    reactiveDesc.renderSize = renderSize;
    reactiveDesc.scale = 1.0f;
    reactiveDesc.cutoffThreshold = 0.2f;
    reactiveDesc.binaryValue = 0.9f;
    reactiveDesc.flags = FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_TONEMAP |
                         FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_THRESHOLD |
                         FFX_UPSCALE_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;
    if (g_ffx.Dispatch(&context, &reactiveDesc.header) != FFX_API_RETURN_OK) {
        failFsr("the reactive mask dispatch failed");
        return false;
    }

    // The mask goes from written to read, and the output is written whole next.
    cmdBuf.recordPipelineImageBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {
            imageBarrier(reactive->texture.image,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            imageBarrier(output->texture.image,
                         0, VK_ACCESS_SHADER_WRITE_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        }
    );

    ffxDispatchDescUpscale upscaleDesc = {};
    upscaleDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    upscaleDesc.commandList = nativeCmd;
    upscaleDesc.color = describeResource(color, colorFormat, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscaleDesc.depth = describeResource(depth, FFX_API_SURFACE_FORMAT_R32_FLOAT, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscaleDesc.motionVectors = describeResource(motionVectors, FFX_API_SURFACE_FORMAT_R16G16_FLOAT, FFX_API_RESOURCE_USAGE_READ_ONLY, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscaleDesc.exposure = {};
    upscaleDesc.reactive = describeResource(reactive, reactiveFormat, FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscaleDesc.transparencyAndComposition = {};
    upscaleDesc.output = describeResource(output, colorFormat, FFX_API_RESOURCE_USAGE_UAV, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    upscaleDesc.jitterOffset = {params.jitterX, params.jitterY};
    // The vectors are in UV units, so the scale takes them to render pixels.
    upscaleDesc.motionVectorScale = {(float) m_renderWidth, (float) m_renderHeight};
    upscaleDesc.renderSize = renderSize;
    upscaleDesc.upscaleSize = displaySize;
    upscaleDesc.enableSharpening = true;
    upscaleDesc.sharpness = std::clamp(params.sharpness, 0.0f, 1.0f);
    upscaleDesc.frameTimeDelta = std::max(params.frameTimeMs, 0.0f);
    upscaleDesc.preExposure = 1.0f;
    upscaleDesc.reset = params.reset || m_resetPending;
    upscaleDesc.cameraNear = params.cameraNear;
    upscaleDesc.cameraFar = params.cameraFar;
    upscaleDesc.cameraFovAngleVertical = params.cameraFovY;
    // World units are yards.
    upscaleDesc.viewSpaceToMetersFactor = 0.9144f;
    // The colour space the context was created non-linear for.
    upscaleDesc.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
    if (g_ffx.Dispatch(&context, &upscaleDesc.header) != FFX_API_RETURN_OK) {
        failFsr("the upscale dispatch failed");
        return false;
    }
    m_resetPending = false;

    // The result is drawn onto the swapchain by a fragment shader.
    cmdBuf.recordPipelineImageBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            imageBarrier(output->texture.image,
                         VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        }
    );
    return true;
#else
    return false;
#endif
}
