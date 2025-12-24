#include "IndirectRenderer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"

namespace indirect {

void IndirectRenderer::init(RHIRenderer* renderer, std::shared_ptr<scene::ModelDOD> model) {
    m_renderer = renderer;
    m_model = model;

    createPipeline();
    buildBuffers();
    
    // Initial upload of static data
    uploadMaterialData();
}

void IndirectRenderer::update(float dt) {
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
            
            // firstInstance links the draw cmd to the instance data array
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

void IndirectRenderer::uploadMaterialData() {
    scene::GLTFUnifiedDODContext tempCtx;
    tempCtx.renderer = m_renderer;
    tempCtx.model = m_model;
    
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

void IndirectRenderer::draw(rhi::RHICommandBuffer* cmd, const scene::Camera& camera) {
    if (m_drawCount == 0 || m_pipeline == INVALID_PIPELINE_HANDLE) return;

    cmd->bindPipeline(m_renderer->getPipeline(m_pipeline));

    // Bind Global Index Buffer (Unified)
    cmd->bindIndexBuffer(m_renderer->getBuffer(m_model->indexBuffer), 0, false);

    PushConstants pc{};
    pc.viewProj = camera.viewProj();
    pc.transformBufferAddr = m_renderer->getBuffer(m_transformBuffer)->getDeviceAddress();
    pc.instanceBufferAddr = m_renderer->getBuffer(m_instanceDataBuffer)->getDeviceAddress();
    pc.vertexBufferAddr = m_renderer->getBuffer(m_model->vertexBuffer)->getDeviceAddress();
    pc.materialBufferAddr = m_renderer->getBuffer(m_materialBuffer)->getDeviceAddress();

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
