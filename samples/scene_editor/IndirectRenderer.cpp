#include "IndirectRenderer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"

// We need the GPU material struct definition. 
// It's usually generated in the build directory.
// For now, we will rely on GLTFUnifiedDOD's uploadMaterials to manage the buffer initially.
// But for updateMaterial, we need the struct.
#include "generated/indirect.frag.h"

namespace indirect {

void IndirectRenderer::init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model) {
    m_renderer = renderer;
    m_model = model;

    createPipeline();
    buildBuffers();
    
    // Initial upload of static data
    uploadMaterialData();
    updateGlobalTransforms();
}

void IndirectRenderer::updateGlobalTransforms() {
    const auto& scene = m_model->scene();
    if (scene.global.empty()) return;

    size_t dataSize = scene.global.size() * sizeof(glm::mat4);

    if (m_transformBuffer == INVALID_BUFFER_HANDLE || 
        m_renderer->getBuffer(m_transformBuffer)->size() < dataSize) 
    {
        m_transformBuffer = m_renderer->createBuffer({
            .size = dataSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .debugName = "IndirectTransforms"
        });
    }

    m_renderer->getBuffer(m_transformBuffer)->uploadData(scene.global.data(), dataSize);
}

void IndirectRenderer::updateMaterial(uint32_t materialIndex) {
    if (materialIndex >= m_model->materials().size()) return;
    if (m_materialBuffer == INVALID_BUFFER_HANDLE) return;

    const auto& mat = m_model->materials()[materialIndex];
    
    // Conversion logic (same as in GLTFUnifiedDOD.cpp)
    auto resolveNormalDefault = [&](TextureHandle handle) -> uint32_t {
        if (handle == INVALID_TEXTURE_HANDLE) handle = m_renderer->getFlatNormalTexture();
        return m_renderer->getTextureBindlessIndex(handle);
    };
    auto resolveTextureWhiteDefault = [&](TextureHandle handle) -> uint32_t {
        if (handle == INVALID_TEXTURE_HANDLE) handle = m_renderer->getWhiteTexture();
        return m_renderer->getTextureBindlessIndex(handle);
    };
    auto resolveTextureBlackDefault = [&](TextureHandle handle) -> uint32_t {
        if (handle == INVALID_TEXTURE_HANDLE) handle = m_renderer->getBlackTexture();
        return m_renderer->getTextureBindlessIndex(handle);
    };
    auto resolveSampler = [&](rhi::SamplerAddressMode mode) -> uint32_t {
        return m_renderer->getBindlessSamplerIndex(mode);
    };

    ShaderGen::indirect_frag::MetallicRoughnessDataGPU d{};
    d.baseColorFactor = mat.m_baseColorFactor;
    d.metallicRoughnessNormalOcclusion = glm::vec4(
        mat.m_metallicFactor,
        mat.m_roughnessFactor,
        mat.m_normalScale,
        mat.m_occlusionStrength
    );
    d.emissiveFactorAlphaCutoff = glm::vec4(mat.m_emissiveFactor * mat.m_emissiveStrength, mat.m_alphaCutoff);
    d.specularGlossiness = glm::vec4(mat.m_specularFactor, mat.m_glossinessFactor);
    d.specularFactors = glm::vec4(mat.m_specularColorFactor, mat.m_specularFactorScalar);
    d.clearcoatTransmissionThickness = glm::vec4(mat.m_clearcoatFactor, mat.m_clearcoatRoughnessFactor,
                                                 mat.m_transmissionFactor, mat.m_volumeThicknessFactor);

    d.occlusionTexture = resolveTextureWhiteDefault(mat.m_occlusionTexture);
    d.occlusionTextureSampler = resolveSampler(mat.m_occlusionSampler);
    d.occlusionTextureUV = mat.m_occlusionUV;
    d.emissiveTexture = resolveTextureBlackDefault(mat.m_emissiveTexture);
    d.emissiveTextureSampler = resolveSampler(mat.m_emissiveSampler);
    d.emissiveTextureUV = mat.m_emissiveUV;
    d.baseColorTexture = resolveTextureWhiteDefault(mat.m_baseColorTexture);
    d.baseColorTextureSampler = resolveSampler(mat.m_baseColorSampler);
    d.baseColorTextureUV = mat.m_baseColorUV;
    d.metallicRoughnessTexture = resolveTextureWhiteDefault(mat.m_metallicRoughnessTexture);
    d.metallicRoughnessTextureSampler = resolveSampler(mat.m_metallicRoughnessSampler);
    d.metallicRoughnessTextureUV = mat.m_metallicRoughnessUV;
    d.normalTexture = resolveNormalDefault(mat.m_normalTexture);
    d.normalTextureSampler = resolveSampler(mat.m_normalSampler);
    d.normalTextureUV = mat.m_normalUV;

    d.clearCoatTexture = resolveTextureWhiteDefault(mat.m_clearcoatTexture);
    d.clearCoatTextureSampler = resolveSampler(mat.m_clearcoatSampler);
    d.clearCoatTextureUV = mat.m_clearcoatUV;
    d.clearCoatRoughnessTexture = resolveTextureWhiteDefault(mat.m_clearcoatRoughnessTexture);
    d.clearCoatRoughnessTextureSampler = resolveSampler(mat.m_clearcoatRoughnessSampler);
    d.clearCoatRoughnessTextureUV = mat.m_clearcoatRoughnessUV;
    d.clearCoatNormalTexture = resolveNormalDefault(mat.m_clearcoatNormalTexture);
    d.clearCoatNormalTextureSampler = resolveSampler(mat.m_clearcoatNormalSampler);
    d.clearCoatNormalTextureUV = mat.m_clearcoatNormalUV;

    d.specularTexture = resolveTextureWhiteDefault(mat.m_specularTexture);
    d.specularTextureSampler = resolveSampler(mat.m_specularSampler);
    d.specularTextureUV = mat.m_specularUV;
    d.specularColorTexture = resolveTextureWhiteDefault(mat.m_specularColorTexture);
    d.specularColorTextureSampler = resolveSampler(mat.m_specularColorSampler);
    d.specularColorTextureUV = mat.m_specularColorUV;
    d.transmissionTexture = resolveTextureWhiteDefault(mat.m_transmissionTexture);
    d.transmissionTextureSampler = resolveSampler(mat.m_transmissionSampler);
    d.transmissionTextureUV = mat.m_transmissionUV;
    d.thicknessTexture = resolveTextureWhiteDefault(mat.m_volumeThicknessTexture);
    d.thicknessTextureSampler = resolveSampler(mat.m_volumeThicknessSampler);
    d.thicknessTextureUV = mat.m_volumeThicknessUV;
    d.attenuation = glm::vec4(mat.m_volumeAttenuationColor, mat.m_volumeAttenuationDistance);
    d.sheenFactors = glm::vec4(mat.m_sheenColorFactor, mat.m_sheenRoughnessFactor);
    d.sheenColorTexture = resolveTextureWhiteDefault(mat.m_sheenColorTexture);
    d.sheenColorTextureSampler = resolveSampler(mat.m_sheenColorSampler);
    d.sheenColorTextureUV = mat.m_sheenColorUV;
    d.sheenRoughnessTexture = resolveTextureWhiteDefault(mat.m_sheenRoughnessTexture);
    d.sheenRoughnessTextureSampler = resolveSampler(mat.m_sheenRoughnessSampler);
    d.sheenRoughnessTextureUV = mat.m_sheenRoughnessUV;

    d.alphaMode = mat.m_alphaMode;
    d.ior = mat.m_ior;
    uint32_t flags = 0;
    if (!mat.m_isSpecularGlossiness) flags |= 1; // MaterialType_MetallicRoughness
    else flags |= 2; // MaterialType_SpecularGlossiness
    if (mat.m_isUnlit) flags |= 4; // MaterialType_Unlit
    if (mat.m_hasSpecular) flags |= 8; // MaterialType_Specular
    if (mat.m_clearcoatFactor > 0.0f) flags |= 16; // MaterialType_ClearCoat
    if (mat.m_transmissionFactor > 0.0f) flags |= 32; // MaterialType_Transmission
    if (mat.m_volumeThicknessFactor > 0.0f) flags |= 64; // MaterialType_Volume
    if (glm::length(mat.m_sheenColorFactor) > 0.0f) flags |= 128; // MaterialType_Sheen

    d.materialType = flags;

    uint64_t offset = materialIndex * sizeof(ShaderGen::indirect_frag::MetallicRoughnessDataGPU);
    m_renderer->getBuffer(m_materialBuffer)->uploadData(&d, sizeof(d), offset);
}

void IndirectRenderer::buildBuffers() {
    std::vector<IndirectCommand> commands;
    std::vector<DrawInstanceData> instances;

    const auto& scene = m_model->scene();
    const auto& meshes = m_model->meshes();

    for (uint32_t nodeId : scene.topoOrder) {
        int32_t meshIdx = -1;
        if (nodeId < scene.meshIndex.size()) meshIdx = scene.meshIndex[nodeId];
        if (meshIdx < 0) continue;

        const auto& mesh = meshes[meshIdx];

        for (const auto& prim : mesh.primitives) {
            IndirectCommand cmd{};
            cmd.indexCount = prim.indexCount;
            cmd.instanceCount = 1;
            cmd.firstIndex = prim.firstIndex;
            cmd.vertexOffset = prim.vertexOffset;
            cmd.firstInstance = (uint32_t)instances.size(); 

            DrawInstanceData inst{};
            inst.transformIndex = nodeId;
            inst.materialIndex = prim.materialIndex;
            
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

        m_instanceBuffer = m_renderer->createBuffer({
            .size = instances.size() * sizeof(DrawInstanceData),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::CPUToGPU,
            .data = instances.data(),
            .debugName = "InstanceDataBuffer"
        });
    }
}

void IndirectRenderer::uploadMaterialData() {
    scene::GLTFUnifiedDODContext tempCtx;
    tempCtx.renderer = m_renderer;
    tempCtx.model = m_model.get();
    
    scene::uploadMaterials(tempCtx);
    
    m_materialBuffer = tempCtx.materialBuffer;
}

void IndirectRenderer::createPipeline() {
    // Note: Shaders will be in the sample's shader directory
    auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/indirect.vert.spv");
    auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/indirect.frag.spv");

    rhi::RHIPipelineBuilder builder;
    builder.setShaders(vert.get(), frag.get())
           .setTopology(rhi::PrimitiveTopology::TriangleList)
           .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
           .setColorFormat(m_renderer->getDrawColorFormat())
           .setDepthFormat(m_renderer->getDrawDepthFormat());

    builder.setPolygonMode(rhi::PolygonMode::Fill).setName("IndirectSolid");
    m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());

    builder.setPolygonMode(rhi::PolygonMode::Line).setName("IndirectWireframe");
    m_pipelineWireframe = m_renderer->createGraphicsPipeline(builder.buildGraphics());
}

void IndirectRenderer::draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera) {
    PipelineHandle activePipeline = m_drawWireframe ? m_pipelineWireframe : m_pipeline;
    if (m_drawCount == 0 || activePipeline == INVALID_PIPELINE_HANDLE) return;

    cmd->bindPipeline(m_renderer->getPipeline(activePipeline));
    cmd->bindIndexBuffer(m_renderer->getBuffer(m_model->indexBuffer), 0, false);

    PushConstants pc{};
    pc.viewProj = camera.viewProj();
    pc.transformBufferAddr = m_renderer->getBuffer(m_transformBuffer)->getDeviceAddress();
    pc.instanceBufferAddr = m_renderer->getBuffer(m_instanceBuffer)->getDeviceAddress();
    pc.vertexBufferAddr = m_renderer->getBuffer(m_model->vertexBuffer)->getDeviceAddress();
    pc.materialBufferAddr = m_renderer->getBuffer(m_materialBuffer)->getDeviceAddress();

    m_renderer->pushConstants(cmd, activePipeline, rhi::ShaderStage::Vertex, pc);

    cmd->drawIndexedIndirect(
        m_renderer->getBuffer(m_indirectBuffer), 
        0, 
        m_drawCount, 
        sizeof(IndirectCommand)
    );
}

void IndirectRenderer::setWireframe(bool enabled) {
    m_drawWireframe = enabled;
}

} // namespace indirect
