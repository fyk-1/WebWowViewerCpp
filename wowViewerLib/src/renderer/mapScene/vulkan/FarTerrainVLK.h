#ifndef AWEBWOWVIEWERCPP_FARTERRAINVLK_H
#define AWEBWOWVIEWERCPP_FARTERRAINVLK_H

#include <functional>
#include <memory>
#include <vector>
#include "../MapScenePlan.h"
#include "../../../gapi/vulkan/GDeviceVulkan.h"
#include "../../../gapi/vulkan/buffers/CBufferChunkVLK.h"
#include "../../../gapi/vulkan/commandBuffer/commandBufferRecorder/CommandBufferRecorder.h"
#include "../../../gapi/vulkan/materials/ISimpleMaterialVLK.h"
#include "../../../gapi/vulkan/shaders/ShaderConfig.h"

/*
 * The GPU half of the far terrain: the whole map's geometry in one vertex and one index buffer, a material for each tile that
 * carries its minimap, and the draw in the forward pass, through the map-area viewport. The map's FarTerrain owns the data and decides, frame by frame, which
 * tiles to draw; this class only turns that into buffers and draw calls. The renderer owns one for as long as it lives, and it
 * follows whichever FarTerrain the plan names.
 */
class FarTerrainVLK {
public:
    // The block of the far pass, set 1: the view-projection, the fade of the swap (the share of the full scene that shows, and the
    // depth band of the draw that takes the rest), and the camera the vertices are taken relative to.
    struct FrameBlock {
        mathfu::mat4 viewProjection;
        mathfu::vec4_packed fade;
        mathfu::vec4_packed camera;
    };

    // sceneShaderConfig is the forward pass's, whose set 0 the far shaders share. The buffers the far terrain uploads are added to
    // uploadBuffers, which the renderer uploads every frame.
    FarTerrainVLK(const HGDeviceVLK &device,
                  const HGBufferVLK &uboBuffer,
                  const ShaderConfig &sceneShaderConfig,
                  const std::shared_ptr<GRenderPassVLK> &forwardPass,
                  const std::shared_ptr<GDescriptorSet> &sceneWideDS,
                  std::vector<std::reference_wrapper<HGBufferVLK>> &uploadBuffers);

    // Update stage: writes the frame's block. The jitter is the upscaler's, in pixels of width x height.
    void write(const FarTerrainFrame &frame, float jitterX, float jitterY, int width, int height);

    // Render thread, before the frame's uploads: takes in the geometry in slices, and makes a material for each tile whose texture
    // has landed.
    void postLoad(const FarTerrainFrame &frame);

    // Makes the far pass's pipeline before any terrain is loaded, with stand-in buffers, and returns it for the caller to hold:
    // the pipeline is the one the tiles' materials will ask for. Null when it could not be made.
    HPipelineVLK createPipelineAhead();

    // Forward pass: one draw for each tile of the frame, and while the full scene fades a second for each, taking its pixels.
    void draw(CmdBufRecorder &cmd, const FarTerrainFrame &frame);

private:
    void createBuffers(const FarTerrain::Geometry &geometry);
    void createMaterials();
    bool heightsUsable();
    void postLoadLandmarks(const FarTerrainFrame &frame);
    // flags are the draw's own instance flags: 0 for the far land's band, 2 for the draw that takes pixels from the fading full scene.
    void drawLandmarks(CmdBufRecorder &cmd, const FarTerrainFrame &frame, uint32_t flags);

    HGDeviceVLK m_device;
    HGBufferVLK m_uboBuffer;
    ShaderConfig m_shaderConfig;
    std::shared_ptr<GRenderPassVLK> m_forwardPass;
    std::shared_ptr<GDescriptorSet> m_sceneWideDS;
    std::vector<std::reference_wrapper<HGBufferVLK>> &m_uploadBuffers;
    bool m_registered = false;

    std::shared_ptr<CBufferChunkVLK<FrameBlock>> m_block;

    // What the current FarTerrain has in the buffers.
    std::shared_ptr<FarTerrain> m_terrain;
    HGBufferVLK m_vertexBuffer;
    HGBufferVLK m_indexBuffer;
    size_t m_vertexBytes = 0;
    size_t m_indexBytes = 0;
    size_t m_uploadedBytes = 0;
    bool m_ready = false;
    HGVertexBufferBindings m_bindings;

    // The material every tile derives from, and the one that draws the tiles whose texture has not landed.
    std::shared_ptr<ISimpleMaterialVLK> m_gapMaterial;
    std::vector<std::shared_ptr<ISimpleMaterialVLK>> m_tileMaterials;

    // The baked heights of each tile as a texture, null for a tile with none or one that has not got to its turn. A tile's draw takes the
    // shared grid once its texture has been seen uploaded (m_heightsLanded), and its own geometry until then.
    std::vector<HGSamplableTexture> m_heightTextures;
    std::vector<uint8_t> m_heightsLanded;
    // The baked colour of each tile as a texture, null for a tile that wears its minimap.
    std::vector<HGSamplableTexture> m_colorTextures;
    // Whether the device samples R16_UNORM with a linear filter, as the heights need; unknown (-1) until the first height texture.
    int m_heightFormatUsable = -1;

    // The landmarks in the buffers below (FarLandmarks): their geometry, uploaded in slices as the far land's is, a material for each of
    // their textures once it has landed, all from a base that draws both faces.
    std::shared_ptr<const FarLandmarks> m_landmarks;
    HGBufferVLK m_landmarkVertexBuffer;
    HGBufferVLK m_landmarkIndexBuffer;
    HGVertexBufferBindings m_landmarkBindings;
    size_t m_landmarkVertexBytes = 0;
    size_t m_landmarkIndexBytes = 0;
    size_t m_landmarkUploadedBytes = 0;
    bool m_landmarkRegistered = false;
    bool m_landmarksReady = false;
    std::shared_ptr<ISimpleMaterialVLK> m_landmarkBase;
    std::vector<std::shared_ptr<ISimpleMaterialVLK>> m_landmarkMaterials;
};

#endif //AWEBWOWVIEWERCPP_FARTERRAINVLK_H
