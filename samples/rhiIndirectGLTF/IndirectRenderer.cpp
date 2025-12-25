#include "IndirectRenderer.hpp"

#include "generated/indirect.frag.h"
#include "generated/indirect.vert.h"
#include "generated/skinning.comp.h"

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/scene/AnimationSystem.hpp"

namespace indirect {

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

    // Allocate skinned buffer (same size as source)
    if (m_model->vertexBuffer != INVALID_BUFFER_HANDLE) {
        auto* srcBuf = m_renderer->getBuffer(m_model->vertexBuffer);
        m_skinnedVertexBuffer = m_renderer->createBuffer({
            .size = srcBuf->size(),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::VertexBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .debugName = "SkinnedVertexBuffer"
        });
    }

    createPipeline();
    buildBuffers();

    // Phase 2: per-mesh xforms used by compute to convert world-space skinned results back to mesh-local.
    // Allocate at least 1 element to keep device address valid.
    const uint32_t meshCount = (uint32_t)m_model->meshes().size();
    const uint32_t count = (meshCount > 0) ? meshCount : 1u;
    m_meshXformsBuffer = m_renderer->createBuffer({
        .size = sizeof(MeshXformGPU) * count,
        .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
        .memoryUsage = rhi::MemoryUsage::CPUToGPU,
        .debugName = "MeshXformsBuffer"
    });
    
    // Initial upload of static data
    uploadMaterialData();
    uploadEnvironmentData(brdf, irradiance, prefilter);
}

void IndirectRenderer::createComputePipeline() {
    auto comp = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/skinning.comp.spv");
    
    rhi::RHIPipelineBuilder builder;
    builder.setComputeShader(comp.get());
    m_skinningPipeline = m_renderer->createComputePipeline(builder.buildCompute());
}

void IndirectRenderer::update(float dt) {
    if (m_model) {
        scene::AnimationSystem::update(*m_model, dt);
        m_model->scene().recalculateGlobalTransformsDirty();
        
        // Update Skinning Matrices
        auto jointMatrices = scene::AnimationSystem::updateSkinning(*m_model);
        if (!jointMatrices.empty()) {
            size_t dataSize = jointMatrices.size() * sizeof(glm::mat4);
            if (m_jointMatricesBuffer == INVALID_BUFFER_HANDLE || 
                m_renderer->getBuffer(m_jointMatricesBuffer)->size() < dataSize) {
                m_jointMatricesBuffer = m_renderer->createBuffer({
                    .size = dataSize,
                    .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                    .memoryUsage = rhi::MemoryUsage::CPUToGPU,
                    .debugName = "JointMatrices"
                });
            }
            m_renderer->getBuffer(m_jointMatricesBuffer)->uploadData(jointMatrices.data(), dataSize);
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
        // If you want stricter behavior, replace this with an explicit mesh->node mapping in your loader.
        std::vector<bool> filled(meshCount, false);
        for (uint32_t nodeId : sc.topoOrder) {
            if (nodeId >= sc.meshIndex.size()) continue;
            const int32_t meshIdx = sc.meshIndex[nodeId];
            if (meshIdx < 0 || (uint32_t)meshIdx >= meshCount) continue;

            // Prefer nodes that actually have a skin (since this xform is only needed to "undo" world-space skinning).
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
            // localNormal = transpose(mat3(model)) * worldNormal
            xf.normalWorldToLocal[0] = glm::vec4(glm::transpose(m3)[0], 0.0f);
            xf.normalWorldToLocal[1] = glm::vec4(glm::transpose(m3)[1], 0.0f);
            xf.normalWorldToLocal[2] = glm::vec4(glm::transpose(m3)[2], 0.0f);

            xforms[(uint32_t)meshIdx] = xf;
            filled[(uint32_t)meshIdx] = true;
        }

        if (m_meshXformsBuffer != INVALID_BUFFER_HANDLE) {
            const size_t bytes = sizeof(MeshXformGPU) * xforms.size();
            m_renderer->getBuffer(m_meshXformsBuffer)->uploadData(xforms.data(), bytes);
        }
    }

    // Update SceneGraph transforms
    const auto& scene = m_model->scene();
    
    if (scene.global.empty()) return;

    size_t dataSize = scene.global.size() * sizeof(glm::mat4);

    if (m_transformBuffer == INVALID_BUFFER_HANDLE || 
        m_renderer->getBuffer(m_transformBuffer)->size() < dataSize) 
    {
        // Reallocate
        m_transformBuffer = m_renderer->createBuffer({
            .size = dataSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "IndirectTransforms"
        });
    }

    // Upload
    m_renderer->getBuffer(m_transformBuffer)->uploadData(scene.global.data(), dataSize);

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
             if (m_jointBuffer == INVALID_BUFFER_HANDLE || m_renderer->getBuffer(m_jointBuffer)->size() < size) {
                 m_jointBuffer = m_renderer->createBuffer({
                     .size = size,
                     .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
                     .memoryUsage = rhi::MemoryUsage::CPUToGPU, 
                     .debugName = "JointBuffer"
                 });
             }
             m_renderer->getBuffer(m_jointBuffer)->uploadData(joints.data(), size);
         }
    }
}

void IndirectRenderer::buildBuffers() {
    std::vector<IndirectCommand> commands;
    std::vector<DrawInstanceData> instances;

    const auto& scene = m_model->scene();
    const auto& meshes = m_model->meshes();

    // Traverse scene in topological order (Parent -> Child)
    for (uint32_t nodeId : scene.topoOrder) {
        int32_t meshIdx = -1;
        if (nodeId < scene.meshIndex.size()) meshIdx = scene.meshIndex[nodeId];
        if (meshIdx < 0) continue;

        const auto& mesh = meshes[meshIdx];

        // For each primitive in the mesh, add a draw command
        for (const auto& prim : mesh.primitives) {
            IndirectCommand cmd{};
            cmd.indexCount = prim.indexCount;
            cmd.instanceCount = 1;
            cmd.firstIndex = prim.firstIndex;
            cmd.vertexOffset = prim.vertexOffset;
            
            // Fix: Set firstInstance to 0 to avoid dependency on 'drawIndirectFirstInstance' feature.
            // We will use gl_DrawID in the shader to index into the instance data.
            cmd.firstInstance = 0; 

            DrawInstanceData inst{};
            inst.transformIndex = nodeId;
            inst.materialIndex = prim.materialIndex;
            inst.jointOffset = -1;
            
            if (nodeId < scene.skinIndex.size()) {
                int32_t sIdx = scene.skinIndex[nodeId];
                if (sIdx >= 0 && (size_t)sIdx < m_skinOffsets.size()) {
                    inst.jointOffset = (int32_t)m_skinOffsets[sIdx];
                }
            }
            
            commands.push_back(cmd);
            instances.push_back(inst);
        }
    }

    m_drawCount = (uint32_t)commands.size();

    if (m_drawCount > 0) {
        m_indirectBuffer = m_renderer->createBuffer({
            .size = commands.size() * sizeof(IndirectCommand),
            .usage = rhi::BufferUsage::IndirectBuffer | rhi::BufferUsage::StorageBuffer,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .data = commands.data(),
            .debugName = "IndirectBuffer"
        });

        m_instanceDataBuffer = m_renderer->createBuffer({
            .size = instances.size() * sizeof(DrawInstanceData),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .data = instances.data(),
            .debugName = "InstanceDataBuffer"
        });
        
        pnkr::core::Logger::info("Built indirect buffers: {} commands", m_drawCount);
    }
}

void IndirectRenderer::uploadEnvironmentData(TextureHandle brdf, TextureHandle irradiance, TextureHandle prefilter) {
    ShaderGen::indirect_frag::EnvironmentMapDataGPU envData{};
    
    // Initialize with INVALID_ID (~0u)
    envData.envMapTexture = ~0u;
    envData.envMapTextureIrradiance = ~0u;
    envData.texBRDF_LUT = ~0u;
    envData.envMapTextureCharlie = ~0u;

    // Get bindless indices from the renderer
    // Assuming sampler index 0 is a valid default linear sampler for the engine
    uint32_t defaultSampler = 0; 

    if (prefilter != INVALID_TEXTURE_HANDLE) {
        envData.envMapTexture = m_renderer->getTextureBindlessIndex(prefilter);
        envData.envMapTextureSampler = defaultSampler;
        
        // Charlie (Sheen) map - using prefilter as placeholder
        envData.envMapTextureCharlie = envData.envMapTexture; 
        envData.envMapTextureCharlieSampler = defaultSampler;
    }
    
    if (irradiance != INVALID_TEXTURE_HANDLE) {
        envData.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(irradiance);
        envData.envMapTextureIrradianceSampler = defaultSampler;
    }
    
    if (brdf != INVALID_TEXTURE_HANDLE) {
        envData.texBRDF_LUT = m_renderer->getTextureBindlessIndex(brdf);
        envData.texBRDF_LUTSampler = defaultSampler;
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
    scene::GLTFUnifiedDODContext tempCtx;
    tempCtx.renderer = m_renderer;
    tempCtx.model = m_model.get();
    
    scene::uploadMaterials(tempCtx);
    
    m_materialBuffer = tempCtx.materialBuffer;
}

void IndirectRenderer::createPipeline() {
    auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/indirect.vert.spv");
    auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/indirect.frag.spv");

    rhi::RHIPipelineBuilder builder;
    builder.setShaders(vert.get(), frag.get())
           .setTopology(rhi::PrimitiveTopology::TriangleList)
           .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
           .setColorFormat(m_renderer->getDrawColorFormat())
           .setDepthFormat(m_renderer->getDrawDepthFormat());

    m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
}

void IndirectRenderer::dispatchSkinning(rhi::RHICommandBuffer* cmd) {
    if (m_drawCount == 0) return;

    // 1. Dispatch Skinning/Morphing if needed
    bool hasSkinning = (m_jointMatricesBuffer != INVALID_BUFFER_HANDLE);
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
        auto* dstBuf = m_renderer->getBuffer(m_skinnedVertexBuffer);
        
        skinPC.inBuffer = srcBuf->getDeviceAddress();
        skinPC.outBuffer = dstBuf->getDeviceAddress();
        if (hasSkinning) {
            skinPC.jointMatrices = m_renderer->getBuffer(m_jointMatricesBuffer)->getDeviceAddress();
            skinPC.hasSkinning = 1;
        }
        if (hasMorphing) {
            skinPC.morphDeltas = m_renderer->getBuffer(m_model->morphVertexBuffer)->getDeviceAddress();
            skinPC.morphStates = m_renderer->getBuffer(m_model->morphStateBuffer)->getDeviceAddress();
            skinPC.hasMorphing = 1;
        }
        if (m_meshXformsBuffer != INVALID_BUFFER_HANDLE) {
            skinPC.meshXforms = m_renderer->getBuffer(m_meshXformsBuffer)->getDeviceAddress();
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

void IndirectRenderer::draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera) {
    if (m_drawCount == 0 || m_pipeline == INVALID_PIPELINE_HANDLE) return;

    cmd->bindPipeline(m_renderer->getPipeline(m_pipeline));

    // Bind Global Index Buffer (Unified)
    cmd->bindIndexBuffer(m_renderer->getBuffer(m_model->indexBuffer), 0, false);

    PushConstants pc{};
    pc.viewProj = camera.viewProj();
    pc.transformBufferAddr = m_renderer->getBuffer(m_transformBuffer)->getDeviceAddress();
    pc.instanceBufferAddr = m_renderer->getBuffer(m_instanceDataBuffer)->getDeviceAddress();
    
    // Bind skinned buffer if compute produced it (skinning and/or morphing)
    const bool hasSkinning = (m_jointMatricesBuffer != INVALID_BUFFER_HANDLE);
    const bool hasMorphing = (m_model->morphVertexBuffer != INVALID_BUFFER_HANDLE && m_model->morphStateBuffer != INVALID_BUFFER_HANDLE);

    if (m_skinnedVertexBuffer != INVALID_BUFFER_HANDLE && (hasSkinning || hasMorphing)) {
        pc.vertexBufferAddr = m_renderer->getBuffer(m_skinnedVertexBuffer)->getDeviceAddress();
    } else {
        pc.vertexBufferAddr = m_renderer->getBuffer(m_model->vertexBuffer)->getDeviceAddress();
    }

    pc.materialBufferAddr = m_renderer->getBuffer(m_materialBuffer)->getDeviceAddress();
    pc.environmentBufferAddr = m_renderer->getBuffer(m_environmentBuffer)->getDeviceAddress();

    m_renderer->pushConstants(cmd, m_pipeline, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

    // Single Indirect Draw Call
    cmd->drawIndexedIndirect(
        m_renderer->getBuffer(m_indirectBuffer), 
        0,              // offset 
        m_drawCount,    // count
        sizeof(IndirectCommand) // stride
    );
}

} // namespace indirect
