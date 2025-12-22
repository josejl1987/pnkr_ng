#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"

#include "../common/RhiSampleApp.hpp"
#include <glm/gtc/matrix_transform.hpp>

// Generated header from shader tool
#include "generated/unlit.vert.h"

using namespace pnkr;

class UnlitSample : public samples::RhiSampleApp {
public:
    UnlitSample() : samples::RhiSampleApp({
        .title = "PNKR - Unlit Rendering",
        .width = 1280,
        .height = 720,
        .createRenderer = false // We manually init to enable bindless
    }) {}

    std::unique_ptr<renderer::scene::Model> m_model;
    renderer::scene::Camera m_camera;
    PipelineHandle m_pipeline;
    float m_rotation = 0.0f;

    void onInit() override {
        // 1. Initialize Renderer with Bindless enabled
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        // 2. Load Model (Uses engine's Model class + fastgltf)
        m_model = renderer::scene::Model::load(*m_renderer, baseDir() / "assets" / "Duck.glb");
        if (!m_model) {
            throw cpptrace::runtime_error("Failed to load Duck.glb");
        }

        // 3. Create Pipeline
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("unlit.vert.spv"));
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("unlit.frag.spv"));

        m_pipeline = m_renderer->createGraphicsPipeline(
            renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .useVertexType<renderer::Vertex>()
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back, true)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("UnlitPipeline")
            .buildGraphics()
        );

        // 4. Setup Camera
        m_camera.lookAt({0.0f, 0.5f, 3.0f}, {0.0f, 0.5f, 0.0f});
        
        initUI(); // Setup ImGui from base class
    }

    void onUpdate(float dt) override {
        m_rotation += dt;
        
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        auto* cmd = ctx.commandBuffer;

        m_renderer->bindPipeline(cmd, m_pipeline);

        // Bind Global Bindless Set (Set 1)
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, bindlessSet);

        // Rotation matrices
        glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), m_rotation, {0, 1, 0});

        // Draw model nodes
        std::function<void(int)> drawNode = [&](int nodeIdx) {
            const auto& node = m_model->nodes()[nodeIdx];
            
            for (const auto& prim : node.m_meshPrimitives) {
                // Get material texture index
                uint32_t texIdx = 0;
                if (prim.m_materialIndex < m_model->materials().size()) {
                    auto texHandle = m_model->materials()[prim.m_materialIndex].m_baseColorTexture;
                    if (texHandle != INVALID_TEXTURE_HANDLE) {
                         texIdx = m_renderer->getTextureBindlessIndex(texHandle);
                    }
                }

                ShaderGen::unlit_vert::PerFrameData pc{};
                pc.model = modelMat * node.m_worldTransform.mat4();
                pc.viewProj = m_camera.viewProj();
                pc.baseColor = glm::vec4(1.0f);
                pc.textureId = texIdx;

                m_renderer->pushConstants(cmd, m_pipeline, renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment, pc);
                
                m_renderer->bindMesh(cmd, prim.m_mesh);
                m_renderer->drawMesh(cmd, prim.m_mesh);
            }

            for (int child : node.m_children) drawNode(child);
        };

        for (int root : m_model->rootNodes()) drawNode(root);
    }
};

int main(int argc, char** argv) {
    UnlitSample app;
    return app.run();
}
