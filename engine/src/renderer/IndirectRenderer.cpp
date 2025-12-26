#include "pnkr/renderer/IndirectRenderer.hpp"

#include "generated/indirect.frag.h"
#include "generated/indirect.vert.h"
#include "generated/skinning.comp.h"

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/scene/AnimationSystem.hpp"

#include <cmath>

namespace pnkr::renderer {

struct MeshXformGPU {
    glm::mat4 invModel;
    glm::mat4 normalWorldToLocal;
};

void IndirectRenderer::init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model,
                            TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter) {
    m_renderer = renderer;
    m_model = model;

    m_skinOffsets.clear();
    uint32_t offset = 0;
    for (const auto& skin : m_model->skins()) {
        m_skinOffsets.push_back(offset);
        offset += (uint32_t)skin.joints.size();
    }

    createComputePipeline();

    uint32_t flightCount = m_renderer->getSwapchain()->framesInFlight();
    m_frames.resize(flightCount);

    const uint32_t meshCount = (uint32_t)m_model->meshes().size();
    const uint32_t xformCount = (meshCount > 0) ? meshCount : 1u;

    // Allocate skinned buffer (same size as source)
    uint64_t skinnedVertexBufferSize = 0;
    if (m_model->vertexBuffer != INVALID_BUFFER_HANDLE) {
        skinnedVertexBufferSize = m_renderer->getBuffer(m_model->vertexBuffer)->size();
    }

    // Initialize per-frame resources
    for (auto& frame : m_frames) {
        if (skinnedVertexBufferSize > 0) {
            frame.skinnedVertexBuffer = m_renderer->createBuffer({
                .size = skinnedVertexBufferSize,
                .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = rhi::MemoryUsage::GPUOnly,
                .debugName = "SkinnedVertexBuffer"
            });
        }

        frame.meshXformsBuffer = m_renderer->createBuffer({
            .size = sizeof(MeshXformGPU) * xformCount,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "MeshXformsBuffer"
        });
        frame.mappedMeshXforms = m_renderer->getBuffer(frame.meshXformsBuffer)->map();
    }

    createPipeline();
    // Note: We don't call buildBuffers() here anymore; GLTFUnifiedDOD handles it in draw()
    
    // Initial upload of static data
    uploadMaterialData();
    uploadEnvironmentData(brdf, irradiance, prefilter);

    // Init offscreen targets
    resize(m_renderer->getSwapchain()->extent().width, m_renderer->getSwapchain()->extent().height);
}

void IndirectRenderer::createOffscreenResources(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;

    // 1. Scene Color (High precision if needed, but matching swapchain for now)
    rhi::TextureDescriptor descRT{};
    descRT.extent = {width, height, 1};
    descRT.format = m_renderer->getSwapchainColorFormat();
    descRT.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::Sampled;
    descRT.mipLevels = 1;
    descRT.debugName = "SceneColor";
    m_sceneColor = m_renderer->createTexture(descRT);
    m_sceneColorLayout = rhi::ResourceLayout::Undefined;

    // 2. Transmission Copy (Needs mips for roughness blur)
    rhi::TextureDescriptor descCopy{};
    descCopy.extent = descRT.extent;
    descCopy.format = descRT.format;
    descCopy.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
    descCopy.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    descCopy.debugName = "TransmissionCopy";
    m_transmissionTexture = m_renderer->createTexture(descCopy);
    m_transmissionLayout = rhi::ResourceLayout::Undefined;
}

void IndirectRenderer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (m_width == width && m_height == height && m_sceneColor != INVALID_TEXTURE_HANDLE) return;

    m_width = width;
    m_height = height;
    createOffscreenResources(width, height);
}

void IndirectRenderer::createComputePipeline() {
    auto comp = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/skinning.comp.spv");
    
    rhi::RHIPipelineBuilder builder;
    builder.setComputeShader(comp.get());
    m_skinningPipeline = m_renderer->createComputePipeline(builder.buildCompute());
}

void IndirectRenderer::update(float dt) {
    if (m_model) {
        m_currentFrameIndex = (m_currentFrameIndex + 1) % m_frames.size();
        auto& frame = m_frames[m_currentFrameIndex];

        scene::AnimationSystem::update(*m_model, dt);
        m_model->scene().recalculateGlobalTransformsDirty();
        
        // Update Skinning Matrices
        auto jointMatrices = scene::AnimationSystem::updateSkinning(*m_model);
        if (!jointMatrices.empty()) {
            size_t dataSize = jointMatrices.size() * sizeof(glm::mat4);
            if (frame.jointMatricesBuffer == INVALID_BUFFER_HANDLE || 
                m_renderer->getBuffer(frame.jointMatricesBuffer)->size() < dataSize) {
                frame.jointMatricesBuffer = m_renderer->createBuffer({
                    .size = dataSize,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "JointMatrices"
                });
                frame.mappedJointMatrices = m_renderer->getBuffer(frame.jointMatricesBuffer)->map();
            }
            
            if (frame.mappedJointMatrices) {
                std::memcpy(frame.mappedJointMatrices, jointMatrices.data(), dataSize);
            } else {
                m_renderer->getBuffer(frame.jointMatricesBuffer)->uploadData(jointMatrices.data(), dataSize);
            }
        }

        // Update Morph States
        if (!m_model->morphStates().empty()) {
            size_t size = m_model->morphStates().size() * sizeof(scene::MorphStateGPU);
            if (m_model->morphStateBuffer == INVALID_BUFFER_HANDLE || 
                m_renderer->getBuffer(m_model->morphStateBuffer)->size() < size) {
                m_model->morphStateBuffer = m_renderer->createBuffer({
                    .size = size,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "MorphStateBuffer"
                });
            }
            m_renderer->getBuffer(m_model->morphStateBuffer)->uploadData(m_model->morphStates().data(), size);
        }

        // Phase 2: update per-mesh xforms (pick one representative node per mesh; prefer skinned nodes).
        const auto& sc = m_model->scene();
        const uint32_t meshCount = (uint32_t)m_model->meshes().size();
        std::vector<MeshXformGPU> xforms;
        xforms.resize(meshCount > 0 ? meshCount : 1u);

        // Default identity
        for (auto& xf : xforms) {
            xf.invModel = glm::mat4(1.0f);
            xf.normalWorldToLocal = glm::mat4(1.0f);
        }

        // Fill from scene: first skinned node that references each mesh wins.
        std::vector<bool> filled(meshCount, false);
        for (uint32_t nodeId : sc.topoOrder) {
            if (nodeId >= sc.meshIndex.size()) continue;
            const int32_t meshIdx = sc.meshIndex[nodeId];
            if (meshIdx < 0 || (uint32_t)meshIdx >= meshCount) continue;

            bool hasSkin = false;
            if (nodeId < sc.skinIndex.size()) {
                const int32_t sIdx = sc.skinIndex[nodeId];
                hasSkin = (sIdx >= 0);
            }
            if (!hasSkin) continue;
            if (filled[(uint32_t)meshIdx]) continue;

            const glm::mat4 model = sc.global[nodeId];
            const glm::mat4 invModel = glm::inverse(model);
            const glm::mat3 m3 = glm::mat3(model);

            MeshXformGPU xf{};
            xf.invModel = invModel;
            xf.normalWorldToLocal = glm::mat4(1.0f);
            xf.normalWorldToLocal[0] = glm::vec4(glm::transpose(m3)[0], 0.0f);
            xf.normalWorldToLocal[1] = glm::vec4(glm::transpose(m3)[1], 0.0f);
            xf.normalWorldToLocal[2] = glm::vec4(glm::transpose(m3)[2], 0.0f);

            xforms[(uint32_t)meshIdx] = xf;
            filled[(uint32_t)meshIdx] = true;
        }

        if (frame.meshXformsBuffer != INVALID_BUFFER_HANDLE) {
            const size_t bytes = sizeof(MeshXformGPU) * xforms.size();
            if (frame.mappedMeshXforms) {
                std::memcpy(frame.mappedMeshXforms, xforms.data(), bytes);
            } else {
                m_renderer->getBuffer(frame.meshXformsBuffer)->uploadData(xforms.data(), bytes);
            }
        }
    }

    updateGlobalTransforms();
}

void IndirectRenderer::updateGlobalTransforms() {
    // Update SceneGraph transforms
    const auto& scene = m_model->scene();
    
    if (scene.global.empty()) return;

    auto& frame = m_frames[m_currentFrameIndex];

    size_t dataSize = scene.global.size() * sizeof(glm::mat4);

    if (frame.transformBuffer == INVALID_BUFFER_HANDLE || 
        m_renderer->getBuffer(frame.transformBuffer)->size() < dataSize) 
    {
        // Reallocate
        frame.transformBuffer = m_renderer->createBuffer({
            .size = dataSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "IndirectTransforms"
        });
        frame.mappedTransform = m_renderer->getBuffer(frame.transformBuffer)->map();
    }

    // Upload
    if (frame.mappedTransform) {
        std::memcpy(frame.mappedTransform, scene.global.data(), dataSize);
    } else {
        m_renderer->getBuffer(frame.transformBuffer)->uploadData(scene.global.data(), dataSize);
    }

    // Update Skins
    if (!m_model->skins().empty()) {
         std::vector<glm::mat4> joints;
         // Estimate size
         uint32_t totalJoints = 0;
         if (!m_skinOffsets.empty()) {
             totalJoints = m_skinOffsets.back() + (uint32_t)m_model->skins().back().joints.size();
         }
         joints.reserve(totalJoints);

         for (const auto& skin : m_model->skins()) {
             for (size_t i = 0; i < skin.joints.size(); ++i) {
                 uint32_t nodeIndex = skin.joints[i];
                 // skin.joints were already offset by +1 in ModelDOD loader fix
                 glm::mat4 global = (nodeIndex < scene.global.size()) ? scene.global[nodeIndex] : glm::mat4(1.0f);
                 glm::mat4 inverseBind = skin.inverseBindMatrices[i];
                 joints.push_back(global * inverseBind);
             }
         }
         
         if (!joints.empty()) {
             size_t size = joints.size() * sizeof(glm::mat4);
             if (frame.jointBuffer == INVALID_BUFFER_HANDLE || m_renderer->getBuffer(frame.jointBuffer)->size() < size) {
                 frame.jointBuffer = m_renderer->createBuffer({
                     .size = size,
                     .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                     .memoryUsage = rhi::MemoryUsage::CPUToGPU, 
                     .debugName = "JointBuffer"
                 });
                 frame.mappedJoints = m_renderer->getBuffer(frame.jointBuffer)->map();
             }
             
             if (frame.mappedJoints) {
                 std::memcpy(frame.mappedJoints, joints.data(), size);
             } else {
                 m_renderer->getBuffer(frame.jointBuffer)->uploadData(joints.data(), size);
             }
         }
    }
}

void IndirectRenderer::buildBuffers() {
    // Legacy - no longer used as GLTFUnifiedDOD handles it
}

void IndirectRenderer::uploadEnvironmentData(TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter) {
    ShaderGen::indirect_frag::EnvironmentMapDataGPU envData{};
    
    // Initialize with INVALID_ID (~0u)
    envData.envMapTexture = ~0u;
    envData.envMapTextureIrradiance = ~0u;
    envData.texBRDF_LUT = ~0u;
    envData.envMapTextureCharlie = ~0u;

    // FIX: Use ClampToEdge sampler for IBL lookups to avoid edge artifacts
    uint32_t clampSampler = m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);

    if (prefilter != INVALID_TEXTURE_HANDLE) {
        envData.envMapTexture = m_renderer->getTextureBindlessIndex(prefilter);
        envData.envMapTextureSampler = clampSampler;
        
        // Charlie (Sheen) map - using prefilter as placeholder if specific map missing
        envData.envMapTextureCharlie = envData.envMapTexture; 
        envData.envMapTextureCharlieSampler = clampSampler;
    }
    
    if (irradiance != INVALID_TEXTURE_HANDLE) {
        envData.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(irradiance);
        envData.envMapTextureIrradianceSampler = clampSampler;
    }
    
    if (brdf != INVALID_TEXTURE_HANDLE) {
        envData.texBRDF_LUT = m_renderer->getTextureBindlessIndex(brdf);
        envData.texBRDF_LUTSampler = clampSampler;
    }

    m_environmentBuffer = m_renderer->createBuffer({
        .size = sizeof(EnvironmentMapDataGPU),
        .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
        .memoryUsage = rhi::MemoryUsage::CPUToGPU,
        .data = &envData,
        .debugName = "EnvironmentBuffer"
    });
}

void IndirectRenderer::uploadMaterialData() {
    if (!m_renderer || !m_model) return;
    m_materialsCPU = scene::packMaterialsGPU(*m_model, *m_renderer);
    uploadMaterialsToGPU();
}

void IndirectRenderer::createPipeline() {
    auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/indirect.vert.spv");
    auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/indirect.frag.spv");

    rhi::RHIPipelineBuilder builder;
    builder.setShaders(vert.get(), frag.get())
           .setTopology(rhi::PrimitiveTopology::TriangleList)
           .setColorFormat(m_renderer->getSwapchainColorFormat()) // Matches m_sceneColor format
           .setDepthFormat(m_renderer->getDrawDepthFormat());

    // Solid Pipeline (Writes Depth)
    builder.setPolygonMode(rhi::PolygonMode::Fill)
           .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
           .setNoBlend()
           .setName("IndirectSolid");
    m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());

    // Transparent Pipeline (No Depth Write, Blend)
    builder.enableDepthTest(false, rhi::CompareOp::LessOrEqual) // Keep test enabled, disable write
           .setAlphaBlend()
           .setName("IndirectTransparent");
    m_pipelineTransparent = m_renderer->createGraphicsPipeline(builder.buildGraphics());

    builder.setPolygonMode(rhi::PolygonMode::Line).setName("IndirectWireframe");
    m_pipelineWireframe = m_renderer->createGraphicsPipeline(builder.buildGraphics());
}

std::span<ShaderGen::indirect_frag::MetallicRoughnessDataGPU> IndirectRenderer::materialsCPU() {
    return m_materialsCPU;
}

void IndirectRenderer::uploadMaterialsToGPU() {
    if (m_materialsCPU.empty()) return;

    const uint64_t bytes = m_materialsCPU.size() * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
    if (m_materialBuffer == INVALID_BUFFER_HANDLE ||
        m_renderer->getBuffer(m_materialBuffer)->size() < bytes)
    {
        m_materialBuffer = m_renderer->createBuffer({
            .size = bytes,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "GLTF Materials"
        });
    }
    m_renderer->getBuffer(m_materialBuffer)->uploadData(m_materialsCPU.data(), bytes);
}

void IndirectRenderer::updateMaterial(uint32_t materialIndex) {
    if (materialIndex >= m_materialsCPU.size()) return;
    if (m_materialBuffer == INVALID_BUFFER_HANDLE) return;

    // Repack if needed from model? Usually materialsCPU is already updated.
    // Assuming m_materialsCPU[materialIndex] is what we want to upload.
    uint64_t offset = materialIndex * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
    m_renderer->getBuffer(m_materialBuffer)->uploadData(&m_materialsCPU[materialIndex], sizeof(m_materialsCPU[materialIndex]), offset);
}

void IndirectRenderer::repackMaterialsFromModel() {
    if (!m_renderer || !m_model) return;
    m_materialsCPU = scene::packMaterialsGPU(*m_model, *m_renderer);
}

void IndirectRenderer::dispatchSkinning(rhi::RHICommandBuffer* cmd) {
    if (m_model->meshes().empty()) return;

    auto& frame = m_frames[m_currentFrameIndex];

    // 1. Dispatch Skinning/Morphing if needed
    bool hasSkinning = (frame.jointMatricesBuffer != INVALID_BUFFER_HANDLE);
    bool hasMorphing = (m_model->morphVertexBuffer != INVALID_BUFFER_HANDLE && m_model->morphStateBuffer != INVALID_BUFFER_HANDLE);

    if ((hasSkinning || hasMorphing) && m_skinningPipeline != INVALID_PIPELINE_HANDLE) {
        cmd->bindPipeline(m_renderer->getPipeline(m_skinningPipeline));
        
        struct SkinPushConstants {
            uint64_t inBuffer;
            uint64_t outBuffer;
            uint64_t jointMatrices;
            uint64_t morphDeltas;
            uint64_t morphStates;
            uint64_t meshXforms;
            uint32_t count;
            uint32_t hasSkinning;
            uint32_t hasMorphing;
        } skinPC{};
        
        auto* srcBuf = m_renderer->getBuffer(m_model->vertexBuffer);
        auto* dstBuf = m_renderer->getBuffer(frame.skinnedVertexBuffer);
        
        skinPC.inBuffer = srcBuf->getDeviceAddress();
        skinPC.outBuffer = dstBuf->getDeviceAddress();
        if (hasSkinning) {
            skinPC.jointMatrices = m_renderer->getBuffer(frame.jointMatricesBuffer)->getDeviceAddress();
            skinPC.hasSkinning = 1;
        }
        if (hasMorphing) {
            skinPC.morphDeltas = m_renderer->getBuffer(m_model->morphVertexBuffer)->getDeviceAddress();
            skinPC.morphStates = m_renderer->getBuffer(m_model->morphStateBuffer)->getDeviceAddress();
            skinPC.hasMorphing = 1;
        }
        if (frame.meshXformsBuffer != INVALID_BUFFER_HANDLE) {
            skinPC.meshXforms = m_renderer->getBuffer(frame.meshXformsBuffer)->getDeviceAddress();
        }

        skinPC.count = (uint32_t)(srcBuf->size() / sizeof(pnkr::renderer::Vertex));
        
        m_renderer->pushConstants(cmd, m_skinningPipeline, rhi::ShaderStage::Compute, skinPC);
        
        cmd->dispatch((skinPC.count + 63) / 64, 1, 1);
        
        // Barrier: Compute Write -> Vertex Attribute Read
        rhi::RHIMemoryBarrier barrier;
        barrier.buffer = dstBuf;
        barrier.srcAccessStage = rhi::ShaderStage::Compute;
        barrier.dstAccessStage = rhi::ShaderStage::Vertex; 
        cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Vertex, {barrier});
    }
}

void IndirectRenderer::draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera, uint32_t width, uint32_t height) {
    if (m_pipeline == INVALID_PIPELINE_HANDLE) return;

    resize(width, height);

    // 1. PREPARE DRAW LISTS (Sorting & Culling)
    scene::GLTFUnifiedDODContext ctx;
    ctx.renderer = m_renderer;
    ctx.model = m_model.get();
    
    // Assign frame-specific buffers to the context
    auto& frame = m_frames[m_currentFrameIndex];
    
    // Assign existing buffers
    ctx.transformBuffer = frame.transformBuffer;
    ctx.indirectOpaqueBuffer = frame.indirectOpaqueBuffer;
    ctx.indirectTransmissionBuffer = frame.indirectTransmissionBuffer;
    ctx.indirectTransparentBuffer = frame.indirectTransparentBuffer;
    
    // Build lists
    scene::GLTFUnifiedDOD::buildDrawLists(ctx, camera.position());
    
    // Update handles back to frame (in case they were recreated)
    frame.transformBuffer = ctx.transformBuffer;
    frame.indirectOpaqueBuffer = ctx.indirectOpaqueBuffer;
    frame.indirectTransmissionBuffer = ctx.indirectTransmissionBuffer;
    frame.indirectTransparentBuffer = ctx.indirectTransparentBuffer;

    // 2. STOP DEFAULT PASS
    cmd->endRendering();

    // 3. COMMON DATA
    ShaderGen::indirect_vert::PerFrameData pc{};
    pc.drawable.view = camera.view();
    pc.drawable.proj = camera.proj();
    pc.drawable.cameraPos = glm::vec4(camera.position(), 1.0f);
    
    // Bind Unified Index Buffer
    cmd->bindIndexBuffer(m_renderer->getBuffer(m_model->indexBuffer), 0, false);

    // Skinning / Vertex Address
    bool hasSkinning = (frame.jointMatricesBuffer != INVALID_BUFFER_HANDLE);
    bool hasMorphing = (m_model->morphVertexBuffer != INVALID_BUFFER_HANDLE);
    if (frame.skinnedVertexBuffer != INVALID_BUFFER_HANDLE && (hasSkinning || hasMorphing)) {
        pc.drawable.vertexBufferPtr = m_renderer->getBuffer(frame.skinnedVertexBuffer)->getDeviceAddress();
    } else {
        pc.drawable.vertexBufferPtr = m_renderer->getBuffer(m_model->vertexBuffer)->getDeviceAddress();
    }
    
    pc.drawable.transformBufferPtr = m_renderer->getBuffer(frame.transformBuffer)->getDeviceAddress();
    pc.drawable.instanceBufferPtr = 0; // Not used in UnifiedDOD logic as it uses gl_InstanceIndex
    
    pc.drawable.materialBufferPtr = m_renderer->getBuffer(m_materialBuffer)->getDeviceAddress();
    pc.drawable.environmentBufferPtr = m_renderer->getBuffer(m_environmentBuffer)->getDeviceAddress();

    auto* sceneColorTex = m_renderer->getTexture(m_sceneColor);
    auto* depthTex = m_renderer->getDepthTexture();

    // --- PHASE 1: Opaque Pass ---
    {
        // Barriers
        std::vector<rhi::RHIMemoryBarrier> barriers;
        rhi::RHIMemoryBarrier bColor{};
        bColor.texture = sceneColorTex;
        bColor.oldLayout = m_sceneColorLayout;
        bColor.newLayout = rhi::ResourceLayout::ColorAttachment;
        bColor.srcAccessStage = rhi::ShaderStage::All;
        bColor.dstAccessStage = rhi::ShaderStage::RenderTarget;
        barriers.push_back(bColor);
        
        // Ensure Depth is ready
        if (m_depthLayout != rhi::ResourceLayout::DepthStencilAttachment) {
            rhi::RHIMemoryBarrier bDepth{};
            bDepth.texture = depthTex;
            bDepth.oldLayout = m_depthLayout;
            bDepth.newLayout = rhi::ResourceLayout::DepthStencilAttachment;
            bDepth.srcAccessStage = rhi::ShaderStage::All;
            bDepth.dstAccessStage = rhi::ShaderStage::DepthStencilAttachment;
            barriers.push_back(bDepth);
            m_depthLayout = rhi::ResourceLayout::DepthStencilAttachment;
        }
        cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::RenderTarget, barriers);
        m_sceneColorLayout = rhi::ResourceLayout::ColorAttachment;

        // Render Info
        rhi::RenderingInfo info{};
        info.renderArea = {0, 0, width, height};
        rhi::RenderingAttachment attColor{};
        attColor.texture = sceneColorTex;
        attColor.loadOp = rhi::LoadOp::Clear;
        attColor.storeOp = rhi::StoreOp::Store;
        attColor.clearValue.isDepthStencil = false;
        attColor.clearValue.color.float32[0] = 0.0f; // Black clear
        info.colorAttachments.push_back(attColor);
        
        rhi::RenderingAttachment attDepth{};
        attDepth.texture = depthTex;
        attDepth.loadOp = rhi::LoadOp::Clear;
        attDepth.storeOp = rhi::StoreOp::Store;
        attDepth.clearValue.isDepthStencil = true;
        attDepth.clearValue.depthStencil.depth = 1.0f;
        info.depthAttachment = &attDepth;

        cmd->beginRendering(info);
        cmd->setViewport({0,0,(float)width,(float)height,0,1});
        cmd->setScissor({0,0,width,height});

        PipelineHandle opaquePipeline = m_drawWireframe ? m_pipelineWireframe : m_pipeline;

        if (!ctx.indirectOpaque.empty()) {
            cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline));
            m_renderer->pushConstants(cmd, opaquePipeline, rhi::ShaderStage::Vertex|rhi::ShaderStage::Fragment, pc);
            
            auto* ib = m_renderer->getBuffer(frame.indirectOpaqueBuffer);
            cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectOpaque.size(), sizeof(rhi::DrawIndexedIndirectCommand));
        }
        cmd->endRendering();
    }

    // --- PHASE 2: Copy Opaque to Transmission ---
    if (!ctx.indirectTransmission.empty()) {
        auto* transTex = m_renderer->getTexture(m_transmissionTexture);

        // Barrier: SceneColor -> TransferSrc, TransTex -> TransferDst
        std::vector<rhi::RHIMemoryBarrier> barriers;
        rhi::RHIMemoryBarrier bSrc{};
        bSrc.texture = sceneColorTex;
        bSrc.oldLayout = m_sceneColorLayout;
        bSrc.newLayout = rhi::ResourceLayout::TransferSrc;
        bSrc.srcAccessStage = rhi::ShaderStage::RenderTarget;
        bSrc.dstAccessStage = rhi::ShaderStage::Transfer;
        barriers.push_back(bSrc);

        rhi::RHIMemoryBarrier bDst{};
        bDst.texture = transTex;
        bDst.oldLayout = m_transmissionLayout;
        bDst.newLayout = rhi::ResourceLayout::TransferDst;
        bDst.srcAccessStage = rhi::ShaderStage::All;
        bDst.dstAccessStage = rhi::ShaderStage::Transfer;
        barriers.push_back(bDst);

        cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::Transfer, barriers);
        m_sceneColorLayout = rhi::ResourceLayout::TransferSrc;
        m_transmissionLayout = rhi::ResourceLayout::TransferDst;

        // Copy
        rhi::TextureCopyRegion region{};
        region.extent = {width, height, 1};
        cmd->copyTexture(sceneColorTex, transTex, region);

        // Mips (Transitions to ShaderReadOnly)
        transTex->generateMipmaps(cmd);
        m_transmissionLayout = rhi::ResourceLayout::ShaderReadOnly;

        // Restore SceneColor
        rhi::RHIMemoryBarrier bRest{};
        bRest.texture = sceneColorTex;
        bRest.oldLayout = rhi::ResourceLayout::TransferSrc;
        bRest.newLayout = rhi::ResourceLayout::ColorAttachment;
        bRest.srcAccessStage = rhi::ShaderStage::Transfer;
        bRest.dstAccessStage = rhi::ShaderStage::RenderTarget;
        cmd->pipelineBarrier(rhi::ShaderStage::Transfer, rhi::ShaderStage::RenderTarget, {bRest});
        m_sceneColorLayout = rhi::ResourceLayout::ColorAttachment;

        // Update PC for transmission lookup
        pc.drawable.transmissionFramebuffer = m_renderer->getTextureBindlessIndex(m_transmissionTexture);
        pc.drawable.transmissionFramebufferSampler = m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);
    } else {
        // Fallback if no transmission needed
        pc.drawable.transmissionFramebuffer = m_renderer->getTextureBindlessIndex(m_renderer->getWhiteTexture());
        pc.drawable.transmissionFramebufferSampler = 0;
    }

    // --- PHASE 3: Transmission & Transparent ---
    {
        rhi::RenderingInfo info{};
        info.renderArea = {0, 0, width, height};
        rhi::RenderingAttachment attColor{};
        attColor.texture = sceneColorTex;
        attColor.loadOp = rhi::LoadOp::Load; // Keep opaque pixels
        attColor.storeOp = rhi::StoreOp::Store;
        info.colorAttachments.push_back(attColor);
        
        rhi::RenderingAttachment attDepth{};
        attDepth.texture = depthTex;
        attDepth.loadOp = rhi::LoadOp::Load; // Keep depth
        attDepth.storeOp = rhi::StoreOp::Store;
        info.depthAttachment = &attDepth;

        cmd->beginRendering(info);
        cmd->setViewport({0,0,(float)width,(float)height,0,1});
        cmd->setScissor({0,0,width,height});

        PipelineHandle opaquePipeline = m_drawWireframe ? m_pipelineWireframe : m_pipeline;

        // Transmission (using Opaque pipeline state usually, but with transmission texture bound)
        if (!ctx.indirectTransmission.empty()) {
            cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline)); // Rebind
            m_renderer->pushConstants(cmd, opaquePipeline, rhi::ShaderStage::Vertex|rhi::ShaderStage::Fragment, pc);
            
            auto* ib = m_renderer->getBuffer(frame.indirectTransmissionBuffer);
            cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectTransmission.size(), sizeof(rhi::DrawIndexedIndirectCommand));
        }

        // Transparent
        if (!ctx.indirectTransparent.empty()) {
            cmd->bindPipeline(m_renderer->getPipeline(m_pipelineTransparent));
            m_renderer->pushConstants(cmd, m_pipelineTransparent, rhi::ShaderStage::Vertex|rhi::ShaderStage::Fragment, pc);
            
            auto* ib = m_renderer->getBuffer(frame.indirectTransparentBuffer);
            cmd->drawIndexedIndirect(ib, 0, (uint32_t)ctx.indirectTransparent.size(), sizeof(rhi::DrawIndexedIndirectCommand));
        }
        cmd->endRendering();
    }

    // --- PHASE 4: Final Blit to Swapchain ---
    {
        auto* backbuffer = m_renderer->getBackbuffer();
        
        // Barriers
        std::vector<rhi::RHIMemoryBarrier> barriers;
        rhi::RHIMemoryBarrier bSrc{};
        bSrc.texture = sceneColorTex;
        bSrc.oldLayout = m_sceneColorLayout;
        bSrc.newLayout = rhi::ResourceLayout::TransferSrc;
        bSrc.srcAccessStage = rhi::ShaderStage::RenderTarget;
        bSrc.dstAccessStage = rhi::ShaderStage::Transfer;
        barriers.push_back(bSrc);

        rhi::RHIMemoryBarrier bDst{};
        bDst.texture = backbuffer;
        // Backbuffer comes in as ColorAttachment from beginFrame
        bDst.oldLayout = rhi::ResourceLayout::ColorAttachment; 
        bDst.newLayout = rhi::ResourceLayout::TransferDst;
        bDst.srcAccessStage = rhi::ShaderStage::RenderTarget;
        bDst.dstAccessStage = rhi::ShaderStage::Transfer;
        barriers.push_back(bDst);

        cmd->pipelineBarrier(rhi::ShaderStage::RenderTarget, rhi::ShaderStage::Transfer, barriers);
        m_sceneColorLayout = rhi::ResourceLayout::TransferSrc;

        // Blit
        rhi::TextureCopyRegion region{};
        region.extent = {width, height, 1};
        cmd->copyTexture(sceneColorTex, backbuffer, region);

        // Restore Backbuffer for UI (ColorAttachment)
        rhi::RHIMemoryBarrier bRest{};
        bRest.texture = backbuffer;
        bRest.oldLayout = rhi::ResourceLayout::TransferDst;
        bRest.newLayout = rhi::ResourceLayout::ColorAttachment;
        bRest.srcAccessStage = rhi::ShaderStage::Transfer;
        bRest.dstAccessStage = rhi::ShaderStage::RenderTarget;
        cmd->pipelineBarrier(rhi::ShaderStage::Transfer, rhi::ShaderStage::RenderTarget, {bRest});
    }

    // --- PHASE 5: Restart Swapchain Pass for ImGui ---
    {
        rhi::RenderingInfo info{};
        info.renderArea = {0, 0, width, height};
        rhi::RenderingAttachment attColor{};
        attColor.texture = m_renderer->getBackbuffer();
        attColor.loadOp = rhi::LoadOp::Load; // Keep blitted image
        attColor.storeOp = rhi::StoreOp::Store;
        info.colorAttachments.push_back(attColor);
        
        cmd->beginRendering(info);
        cmd->setViewport({0,0,(float)width,(float)height,0,1});
        cmd->setScissor({0,0,width,height});
    }
}

void IndirectRenderer::setWireframe(bool enabled) {
    m_drawWireframe = enabled;
}

} // namespace pnkr::renderer
