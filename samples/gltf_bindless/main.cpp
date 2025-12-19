#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/vulkan/pipeline/PipelineBuilder.h"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

// Bypass broken generated header for now
namespace ShaderGen {
    struct MaterialData {
        glm::vec4 baseColorFactor;
        uint32_t baseColorTexture;
        uint32_t _pad0;
        uint32_t _pad1;
        uint32_t _pad2;
    };
    struct PushConstants {
        glm::mat4 model;
        glm::mat4 viewProj;
        uint32_t materialIndex;
    };
}

#include "../common/RhiSampleApp.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <variant>

using namespace pnkr;

class GLTFBindlessApp : public samples::RhiSampleApp {
public:
    GLTFBindlessApp()
        : samples::RhiSampleApp({"RHI Bindless GLTF", 1280, 720, SDL_WINDOW_RESIZABLE, false}) {}

    renderer::scene::Camera m_camera;
    std::unique_ptr<renderer::scene::Model> m_model;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    PipelineHandle m_pipeline;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_materialBuffer;

    void onInit() override {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        m_camera.lookAt({2.0f, 2.0f, 2.0f}, {0.f, 0.5f, 0.f}, {0.f, 1.f, 0.f});

        // Pass RHIRenderer directly. Model::load now expects RHIRenderer&.
        m_model = renderer::scene::Model::load(*m_renderer, baseDir() / "assets" / "Duck.glb");

        if (!m_model) throw std::runtime_error("Failed to load model");

        uploadMaterials();
        createPipeline();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx) {
            recordFrame(ctx);
        });
    }

    void uploadMaterials() {
        const auto& modelMaterials = m_model->materials();
        std::vector<ShaderGen::MaterialData> gpuMaterials;

        for (const auto& mat : modelMaterials) {
            ShaderGen::MaterialData m{};
            m.baseColorFactor = mat.baseColorFactor;
            if (mat.baseColorTexture != INVALID_TEXTURE_HANDLE) {
                m.baseColorTexture = m_renderer->getTextureBindlessIndex(mat.baseColorTexture);
            } else {
                m.baseColorTexture = 0xFFFFFFFF;
            }
            gpuMaterials.push_back(m);
        }

        if (gpuMaterials.empty()) gpuMaterials.push_back({});

        size_t size = gpuMaterials.size() * sizeof(ShaderGen::MaterialData);
        m_materialBuffer = m_renderer->device()->createBuffer({
            .size = size,
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::TransferDst,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
            .debugName = "MaterialBuffer"
        });

        auto staging = m_renderer->device()->createBuffer({
            .size = size,
            .usage = renderer::rhi::BufferUsage::TransferSrc,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = gpuMaterials.data(),
            .debugName = "MaterialStaging"
        });

        auto cmd = m_renderer->device()->createCommandBuffer();
        cmd->begin();
        cmd->copyBuffer(staging.get(), m_materialBuffer.get(), 0, 0, size);
        cmd->end();
        m_renderer->device()->submitCommands(cmd.get());
        m_renderer->device()->waitIdle();
    }

    void createPipeline() {
        // Use renderer::rhi::Shader
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("vertex_pulling.vert.spv"));
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("gltf_bindless.frag.spv"));

        auto builder = renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get())
            .useVertexType<renderer::Vertex>()
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("GltfBindless");

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void recordFrame(const renderer::RHIFrameContext& ctx) {
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        std::function<void(int)> drawNode = [&](int nodeIdx) {
            const auto& node = m_model->nodes()[nodeIdx];

            for (const auto& prim : node.meshPrimitives) {
                ShaderGen::PushConstants pc{};
                pc.model = node.worldTransform.mat4();
                pc.viewProj = m_camera.viewProj();
                pc.materialIndex = prim.materialIndex;

                m_renderer->pushConstants(ctx.commandBuffer, m_pipeline,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment,
                    pc);

                m_renderer->bindMesh(ctx.commandBuffer, prim.mesh);
                m_renderer->drawMesh(ctx.commandBuffer, prim.mesh);
            }

            for (int child : node.children) {
                drawNode(child);
            }
        };

        for (int root : m_model->rootNodes()) {
            drawNode(root);
        }
    }
};

int main(int argc, char** argv) {
    GLTFBindlessApp app;
    return app.run();
}
