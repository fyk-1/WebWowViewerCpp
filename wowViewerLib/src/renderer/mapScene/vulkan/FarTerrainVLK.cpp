#include "FarTerrainVLK.h"
#include "../../../engine/objects/farTerrain/farLandmarks.h"
#include "../../../gapi/vulkan/materials/MaterialBuilderVLK.h"
#include "../../../gapi/vulkan/commandBuffer/commandBufferRecorder/CommandBufferRecorder_inline.h"

namespace {
    /*
    robots@Impl: The geometry goes up in slices.

    The whole map's vertices and indices are one upload of several megabytes, which on one frame would stall it. The buffers are
    made at their full size, and each frame copies a slice into the staging ring, until all of it is there. The far pass draws
    nothing until then.
    */
    constexpr size_t kUploadBytesPerFrame = 2 * 1024 * 1024;

    // The materials made in one frame: each makes a descriptor set, and a tile's texture lands one at a time anyway.
    constexpr int kMaterialsPerFrame = 8;

    /*
    robots@Impl: The depth band of the draw that takes pixels from the fading full scene.

    The scene's window depth runs 0 to 0.99, and a fragment closer than a yard to the camera sits at about 0. The taking draw maps
    the far land's depth into NDC 1e-6 to 2e-6 of the usual viewport, in front of every scene fragment beyond that yard, so it wins
    the depth test where the screen-door lets it, and everything the scene draws later in those pixels (liquids, translucent
    models, weather) fails against it and so dissolves with the rest. A 32-bit float resolves that band to 1e-13, finer than the far
    tiles need. The mapping is in the vertex shader, since a projection matrix cannot hold a band that narrow: its terms are of
    order 1 and cancel to 1e-7.
    */
    constexpr float kTakeDepthLow = 1e-6f;
    constexpr float kTakeDepthSize = 1e-6f;

    const std::vector<GBufferBinding> kBindings = {{
        {0, 3, GBindingType::GFLOAT, false, sizeof(FarTerrainVertex), offsetof(FarTerrainVertex, position)},
        {1, 3, GBindingType::GFLOAT, false, sizeof(FarTerrainVertex), offsetof(FarTerrainVertex, normal)},
        {2, 2, GBindingType::GFLOAT, false, sizeof(FarTerrainVertex), offsetof(FarTerrainVertex, uv)},
    }};
}

FarTerrainVLK::FarTerrainVLK(const HGDeviceVLK &device,
                             const HGBufferVLK &uboBuffer,
                             const ShaderConfig &sceneShaderConfig,
                             const std::shared_ptr<GRenderPassVLK> &forwardPass,
                             const std::shared_ptr<GDescriptorSet> &sceneWideDS,
                             std::vector<std::reference_wrapper<HGBufferVLK>> &uploadBuffers) :
    m_device(device), m_uboBuffer(uboBuffer), m_shaderConfig(sceneShaderConfig), m_forwardPass(forwardPass),
    m_sceneWideDS(sceneWideDS), m_uploadBuffers(uploadBuffers) {

    m_shaderConfig.vertexShaderFolder = "bindless/farTerrain";
    m_shaderConfig.fragmentShaderFolder = "bindless/farTerrain";
    // Set 1 is the far pass's block, which both stages read.
    m_shaderConfig.typeOverrides[1] = {
        {0, {VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT}},
    };

    m_block = std::make_shared<CBufferChunkVLK<FrameBlock>>(m_uboBuffer);
}

void FarTerrainVLK::write(const FarTerrainFrame &frame, const HFrameDependantData &frameData, float jitterX, float jitterY, int width, int height) {
    if (!frame.on || frame.tiles == 0) return;

    mathfu::mat4 viewProjection = frame.viewProjection;
    if (jitterX != 0.0f || jitterY != 0.0f) {
        // The jitter as a shift of NDC, as the scene-wide block applies it to the scene's projection.
        viewProjection = mathfu::mat4::FromTranslationVector(mathfu::vec3(
            2.0f * jitterX / (float) width,
            2.0f * jitterY / (float) height,
            0.0f)) * viewProjection;
    }

    // The fog colour of the frame, the sky cone's own pick: the end colour of the first fog, or the sky's fog colour where that is black.
    mathfu::vec3 fogColor = frameData->skyColors.SkyFogColor;
    if (!frameData->fogResults.empty() && frameData->fogResults[0].EndFogColor.Length() >= 0.0001f) {
        fogColor = frameData->fogResults[0].EndFogColor;
    }

    auto &block = m_block->getObject();
    block.viewProjection = viewProjection;
    block.params = mathfu::vec4_packed(mathfu::vec4(frame.fogStart, frame.extent, frame.fogStrength, 0.0f));
    block.fogColor = mathfu::vec4_packed(mathfu::vec4(fogColor, 0.0f));
    block.fade = mathfu::vec4_packed(mathfu::vec4(frame.share, kTakeDepthLow, kTakeDepthSize, 0.0f));
    block.camera = mathfu::vec4_packed(mathfu::vec4(frame.cameraPos, 0.0f));
    m_block->save();
}

void FarTerrainVLK::createBuffers(const FarTerrain::Geometry &geometry) {
    m_vertexBytes = geometry.vertices.size() * sizeof(FarTerrainVertex);
    m_indexBytes = geometry.indices.size() * sizeof(uint16_t);
    m_uploadedBytes = 0;
    m_ready = false;
    m_gapMaterial = nullptr;

    // The buffers are replaced when the map is, and the renderer's upload list keeps pointing at these two members.
    HGBufferVLK vertexBuffer = m_device->createVertexBuffer("Scene_VBO_FarTerrain", m_vertexBytes, sizeof(FarTerrainVertex));
    HGBufferVLK indexBuffer = m_device->createIndexBuffer("Scene_IBO_FarTerrain", m_indexBytes);
    m_vertexBuffer = vertexBuffer;
    m_indexBuffer = indexBuffer;
    if (!m_registered) {
        m_uploadBuffers.push_back(std::ref(m_vertexBuffer));
        m_uploadBuffers.push_back(std::ref(m_indexBuffer));
        m_registered = true;
    }

    m_bindings = m_device->createVertexBufferBindings();
    m_bindings->addVertexBufferBinding(m_vertexBuffer, kBindings);
    m_bindings->setIndexBuffer(m_indexBuffer);
    m_bindings->save();
}

void FarTerrainVLK::createMaterials() {
    static const PipelineTemplate pipeline = {
        DrawElementMode::TRIANGLES,
        true,
        true,
        EGxBlendEnum::GxBlend_Opaque,
        true,
        true,
        0xFF
    };

    auto block = m_block;
    auto white = m_device->getWhiteTexturePixel();
    m_gapMaterial = MaterialBuilderVLK::fromShader(m_device, {"farTerrain", "farTerrain"}, m_shaderConfig)
        .createPipeline(m_bindings, m_forwardPass, pipeline)
        .bindDescriptorSet(0, m_sceneWideDS)
        .createDescriptorSet(1, [block](std::shared_ptr<GDescriptorSet> &ds) {
            ds->beginUpdate()
                .ubo(0, *block)
                .delayUpdate();
        })
        .createDescriptorSet(2, [white](std::shared_ptr<GDescriptorSet> &ds) {
            ds->beginUpdate()
                .texture(0, white)
                .texture(1, white)
                .delayUpdate();
        })
        .toMaterial();
}

HPipelineVLK FarTerrainVLK::createPipelineAhead() {
    // The pipeline reads the vertex layout and not the buffers, so a binding with none builds the same one.
    m_bindings = m_device->createVertexBufferBindings();
    m_bindings->addVertexBufferBinding(nullptr, kBindings);
    createMaterials();

    HPipelineVLK pipeline = m_gapMaterial != nullptr ? m_gapMaterial->getPipeline() : nullptr;
    m_gapMaterial = nullptr;
    m_bindings = nullptr;
    return pipeline;
}

void FarTerrainVLK::postLoad(const FarTerrainFrame &frame) {
    if (frame.on) postLoadLandmarks(frame);
    if (!frame.on || frame.tiles == 0 || frame.terrain == nullptr) return;

    if (m_terrain != frame.terrain) {
        m_terrain = frame.terrain;
        m_ready = false;
        m_gapMaterial = nullptr;
        m_tileMaterials.assign(m_terrain->tiles().size(), nullptr);
        m_heightTextures.assign(m_terrain->tiles().size(), nullptr);
        m_heightsLanded.assign(m_terrain->tiles().size(), 0);
        m_colorTextures.assign(m_terrain->tiles().size(), nullptr);
        m_vertexBytes = 0;
        m_indexBytes = 0;
        m_uploadedBytes = 0;

        if (m_terrain->geometry() != nullptr) {
            createBuffers(*m_terrain->geometry());
        }
    }

    if (!m_ready) {
        const FarTerrain::Geometry *geometry = m_terrain->geometry();
        if (geometry == nullptr || m_vertexBuffer == nullptr) return;

        // Vertices first, then indices, a slice a frame.
        size_t budget = kUploadBytesPerFrame;
        const size_t total = m_vertexBytes + m_indexBytes;
        while (budget > 0 && m_uploadedBytes < total) {
            if (m_uploadedBytes < m_vertexBytes) {
                const size_t length = std::min(budget, m_vertexBytes - m_uploadedBytes);
                m_vertexBuffer->uploadDataAtOffset((const uint8_t *) geometry->vertices.data() + m_uploadedBytes, (int) length, m_uploadedBytes);
                m_uploadedBytes += length;
                budget -= length;
            } else {
                const size_t indexOffset = m_uploadedBytes - m_vertexBytes;
                const size_t length = std::min(budget, m_indexBytes - indexOffset);
                m_indexBuffer->uploadDataAtOffset((const uint8_t *) geometry->indices.data() + indexOffset, (int) length, indexOffset);
                m_uploadedBytes += length;
                budget -= length;
            }
        }

        if (m_uploadedBytes < total) return;

        createMaterials();
        m_terrain->releaseGeometry();
        m_ready = true;
    }

    // A material for each tile whose texture has landed, the ones in view first.
    int made = 0;
    auto make = [&](size_t index) {
        if (made >= kMaterialsPerFrame || m_tileMaterials[index] != nullptr) return;

        // A tile with baked colour wears it and waits for no minimap; one whose file no longer reads takes its minimap.
        HGSamplableTexture texture = nullptr;
        ColorTile color;
        if (m_terrain->tiles()[index].hasColor && m_terrain->readColor(index, color)) {
            texture = m_device->createTexture(false, false);
            texture->getTexture()->loadData(ColorTile::kSize, ColorTile::kSize, color.rgba.data(), ITextureFormat::itRGBA);
            m_colorTextures[index] = texture;
        } else {
            auto blp = m_terrain->texture(index);
            if (blp == nullptr || blp->getStatus() != FileStatus::FSLoaded) return;

            texture = m_device->createBlpTexture(blp, false, false);
        }

        // The heights go in with the ground texture, since a material's set is made once; a file that no longer reads leaves the tile on its own geometry.
        HGSamplableTexture heights = m_device->getWhiteTexturePixel();
        if (m_terrain->tiles()[index].hasHeights && heightsUsable()) {
            HeightTile data;
            if (m_terrain->readHeights(index, data)) {
                heights = m_device->createTexture(false, false);
                heights->getTexture()->loadData(HeightTile::kSize, HeightTile::kSize, data.v.data(), ITextureFormat::itR16);
                m_heightTextures[index] = heights;
            }
        }

        m_tileMaterials[index] = MaterialBuilderVLK::fromMaterial(m_device, m_gapMaterial)
            .createDescriptorSet(2, [texture, heights](std::shared_ptr<GDescriptorSet> &ds) {
                ds->beginUpdate()
                    .texture(0, texture)
                    .texture(1, heights)
                    .delayUpdate();
            })
            .toMaterial();
        made++;
    };
    for (uint16_t index : frame.drawn) make(index);
    for (size_t index = 0; index < m_tileMaterials.size(); index++) make(index);

    // A height texture seen uploaded in an earlier frame is in its descriptor set, so the tile may take the grid from now on.
    for (size_t index = 0; index < m_heightTextures.size(); index++) {
        if (m_heightsLanded[index] || m_heightTextures[index] == nullptr) continue;
        if (m_heightTextures[index]->getTexture()->getIsLoaded()) m_heightsLanded[index] = 1;
    }
}

bool FarTerrainVLK::heightsUsable() {
    if (m_heightFormatUsable < 0) {
        try {
            static_cast<IDeviceVulkan &>(*m_device).findSupportedFormat({VK_FORMAT_R16_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
            m_heightFormatUsable = 1;
        } catch (const std::exception &) {
            m_heightFormatUsable = 0;
            std::cerr << "far terrain: the device does not sample R16_UNORM with a linear filter, so baked heights are not used" << std::endl;
        }
    }
    return m_heightFormatUsable == 1;
}

void FarTerrainVLK::draw(CmdBufRecorder &cmd, const FarTerrainFrame &frame) {
    // While the full scene fades, the far land takes the pixels the scene's screen-door drops, through the usual viewport and in
    // front of the scene. With the far land alone and the scene gone there is nothing to take from.
    const bool taking = frame.share < 1.0f && !(frame.share <= 0.0f && frame.only);

    if (!m_ready || frame.terrain != m_terrain || frame.drawn.empty()) {
        drawLandmarks(cmd, frame, 0);
        if (taking) drawLandmarks(cmd, frame, 2);
        return;
    }

    cmd.setDefaultScissors();
    cmd.bindVertexBindings(m_bindings);

    const auto &tiles = m_terrain->tiles();
    auto drawTiles = [&](uint32_t flags) {
        for (size_t k = 0; k < frame.drawn.size(); k++) {
            const uint16_t index = frame.drawn[k];
            const auto &tile = tiles[index];
            const auto &material = m_tileMaterials[index];
            const uint32_t level = frame.levels[k];
            const auto &grid = m_terrain->grid((int) level);

            cmd.bindMaterial(material != nullptr ? material : m_gapMaterial);
            // The instance index carries the flags of the vertex shader: bit 0 the tile's texture, bit 1 the taking draw, bit 2 the shared
            // grid displaced by the baked heights, and with it the tile's row from bit 8, its column from bit 14 and the grid's level from
            // bit 20, and bit 5 the baked colour as that texture.
            const bool wearsColor = material != nullptr && m_colorTextures[index] != nullptr;
            const uint32_t instance = flags | (material != nullptr ? 1u : 0u) | (wearsColor ? 32u : 0u);
            if (frame.heights && tile.hasHeights && m_heightsLanded[index] && grid.indexCount > 0) {
                cmd.drawIndexed(grid.indexCount, 1, grid.firstIndex,
                                instance | 4u | ((uint32_t) tile.row << 8) | ((uint32_t) tile.col << 14) | (level << 20), grid.vertexOffset);
            } else {
                cmd.drawIndexed(tile.indexCount, 1, tile.firstIndex, instance, tile.vertexOffset);
            }
        }
    };

    // The map-area viewport, whose depth range lies above every scene fragment and below the sky (FarTerrain: the band of depth).
    cmd.setViewPort(CmdBufRecorder::ViewportType::vp_mapArea);
    drawTiles(0);
    drawLandmarks(cmd, frame, 0);

    // The landmarks take their pixels with the far land, or the taken tiles in front of everything would cover them.
    if (taking) {
        cmd.setViewPort(CmdBufRecorder::ViewportType::vp_usual);
        cmd.bindVertexBindings(m_bindings);
        drawTiles(2);
        drawLandmarks(cmd, frame, 2);
    }
}

void FarTerrainVLK::postLoadLandmarks(const FarTerrainFrame &frame) {
    // A map with none lets go of the last one's geometry and materials. The two buffers stay, since the renderer's upload list
    // reads every buffer it was given.
    if (frame.landmarks == nullptr) {
        if (m_landmarks != nullptr) {
            m_landmarks = nullptr;
            m_landmarksReady = false;
            m_landmarkMaterials.clear();
            m_landmarkBase = nullptr;
            m_landmarkBindings = nullptr;
            m_landmarkVertexBytes = 0;
            m_landmarkIndexBytes = 0;
        }
        return;
    }

    const FarLandmarks::Geometry &geometry = frame.landmarks->geometry();
    if (frame.landmarks != m_landmarks) {
        m_landmarks = frame.landmarks;
        m_landmarksReady = false;
        m_landmarkMaterials.assign(geometry.textures.size(), nullptr);
        m_landmarkVertexBytes = geometry.vertices.size() * sizeof(FarTerrainVertex);
        m_landmarkIndexBytes = geometry.indices.size() * sizeof(uint16_t);
        m_landmarkUploadedBytes = 0;
        if (m_landmarkVertexBytes == 0 || m_landmarkIndexBytes == 0) return;

        // The buffers are replaced when the landmarks are, and the renderer's upload list keeps pointing at these two members.
        HGBufferVLK vertexBuffer = m_device->createVertexBuffer("Scene_VBO_FarLandmarks", m_landmarkVertexBytes, sizeof(FarTerrainVertex));
        HGBufferVLK indexBuffer = m_device->createIndexBuffer("Scene_IBO_FarLandmarks", m_landmarkIndexBytes);
        m_landmarkVertexBuffer = vertexBuffer;
        m_landmarkIndexBuffer = indexBuffer;
        if (!m_landmarkRegistered) {
            m_uploadBuffers.push_back(std::ref(m_landmarkVertexBuffer));
            m_uploadBuffers.push_back(std::ref(m_landmarkIndexBuffer));
            m_landmarkRegistered = true;
        }
        m_landmarkBindings = m_device->createVertexBufferBindings();
        m_landmarkBindings->addVertexBufferBinding(m_landmarkVertexBuffer, kBindings);
        m_landmarkBindings->setIndexBuffer(m_landmarkIndexBuffer);
        m_landmarkBindings->save();
    }
    if (m_landmarkVertexBytes == 0 || m_landmarkIndexBytes == 0) return;

    if (!m_landmarksReady) {
        size_t budget = kUploadBytesPerFrame;
        const size_t total = m_landmarkVertexBytes + m_landmarkIndexBytes;
        while (budget > 0 && m_landmarkUploadedBytes < total) {
            if (m_landmarkUploadedBytes < m_landmarkVertexBytes) {
                const size_t length = std::min(budget, m_landmarkVertexBytes - m_landmarkUploadedBytes);
                m_landmarkVertexBuffer->uploadDataAtOffset((const uint8_t *) geometry.vertices.data() + m_landmarkUploadedBytes, (int) length, m_landmarkUploadedBytes);
                m_landmarkUploadedBytes += length;
                budget -= length;
            } else {
                const size_t indexOffset = m_landmarkUploadedBytes - m_landmarkVertexBytes;
                const size_t length = std::min(budget, m_landmarkIndexBytes - indexOffset);
                m_landmarkIndexBuffer->uploadDataAtOffset((const uint8_t *) geometry.indices.data() + indexOffset, (int) length, indexOffset);
                m_landmarkUploadedBytes += length;
                budget -= length;
            }
        }
        if (m_landmarkUploadedBytes < total) return;

        // Both faces, since a leaf card or a thin wall is seen from either side.
        static const PipelineTemplate pipeline = {
            DrawElementMode::TRIANGLES,
            false,
            true,
            EGxBlendEnum::GxBlend_Opaque,
            true,
            true,
            0xFF
        };
        auto block = m_block;
        auto white = m_device->getWhiteTexturePixel();
        m_landmarkBase = MaterialBuilderVLK::fromShader(m_device, {"farTerrain", "farTerrain"}, m_shaderConfig)
            .createPipeline(m_landmarkBindings, m_forwardPass, pipeline)
            .bindDescriptorSet(0, m_sceneWideDS)
            .createDescriptorSet(1, [block](std::shared_ptr<GDescriptorSet> &ds) {
                ds->beginUpdate()
                    .ubo(0, *block)
                    .delayUpdate();
            })
            .createDescriptorSet(2, [white](std::shared_ptr<GDescriptorSet> &ds) {
                ds->beginUpdate()
                    .texture(0, white)
                    .texture(1, white)
                    .delayUpdate();
            })
            .toMaterial();
        m_landmarksReady = true;
    }

    // A material for each texture that has landed.
    int made = 0;
    auto white = m_device->getWhiteTexturePixel();
    for (size_t slot = 0; slot < m_landmarkMaterials.size() && made < kMaterialsPerFrame; slot++) {
        if (m_landmarkMaterials[slot] != nullptr) continue;
        HBlpTexture blp = geometry.textures[slot];
        if (blp == nullptr || blp->getStatus() != FileStatus::FSLoaded) continue;

        HGSamplableTexture texture = m_device->createBlpTexture(blp, true, true);
        m_landmarkMaterials[slot] = MaterialBuilderVLK::fromMaterial(m_device, m_landmarkBase)
            .createDescriptorSet(2, [texture, white](std::shared_ptr<GDescriptorSet> &ds) {
                ds->beginUpdate()
                    .texture(0, texture)
                    .texture(1, white)
                    .delayUpdate();
            })
            .toMaterial();
        made++;
    }
}

void FarTerrainVLK::drawLandmarks(CmdBufRecorder &cmd, const FarTerrainFrame &frame, uint32_t flags) {
    if (!m_landmarksReady || frame.landmarks == nullptr || frame.landmarks != m_landmarks) return;

    cmd.setDefaultScissors();
    cmd.setViewPort(flags & 2u ? CmdBufRecorder::ViewportType::vp_usual : CmdBufRecorder::ViewportType::vp_mapArea);
    cmd.bindVertexBindings(m_landmarkBindings);
    for (const auto &draw : m_landmarks->geometry().draws) {
        const auto &material = m_landmarkMaterials[draw.texture];
        if (material == nullptr) continue;

        cmd.bindMaterial(material);
        // The instance index carries the flags of the shaders: bit 0 the texture, bit 1 the taking draw, bit 3 a landmark, bit 4 an
        // alpha-keyed material.
        cmd.drawIndexed(draw.indexCount, 1, draw.firstIndex, flags | 1u | 8u | (draw.alphaTest ? 16u : 0u), draw.vertexOffset);
    }
}
