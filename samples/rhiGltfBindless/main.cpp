#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/vulkan/pipeline/PipelineBuilder.h"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"

#include "generated/gltf_bindless.vert.h"

#include "../common/RhiSampleApp.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <iostream>
#include <variant>

using namespace pnkr;

class RHIGltfBindlessApp : public samples::RhiSampleApp {
public:
    RHIGltfBindlessApp()
        : samples::RhiSampleApp({"RHI Bindless GLTF", 1280, 720, SDL_WINDOW_RESIZABLE, false}) {}

    renderer::scene::Camera m_camera;
    std::unique_ptr<renderer::scene::Model> m_model;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    PipelineHandle m_pipeline;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_materialBuffer;
    std::unique_ptr<renderer::rhi::RHITexture> m_dummyTexture;
    std::unique_ptr<renderer::rhi::RHISampler> m_dummySampler;
    std::unique_ptr<renderer::rhi::RHIDescriptorSet> m_materialSet;

    void onInit() override {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        m_camera.lookAt({2.0f, 2.0f, 2.0f}, {0.f, 0.5f, 0.f}, {0.f, 1.f, 0.f});

        std::unique_ptr<renderer::RHIRenderer>::pointer baseRenderer = m_renderer.get();
        m_model = renderer::scene::Model::load(*baseRenderer, baseDir() / "assets" / "structure.glb");

        if (!m_model) throw std::runtime_error("Failed to load model");

        if (!m_dummySampler) {
            m_dummySampler = m_renderer->device()->createSampler(
                renderer::rhi::Filter::Linear,
                renderer::rhi::Filter::Linear,
                renderer::rhi::SamplerAddressMode::Repeat
            );
        }
        uploadMaterials();
        createPipeline();
        createDescriptors();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx) {
            recordFrame(ctx);
        });
    }
    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }
    void uploadMaterials() {
        const auto& modelMaterials = m_model->materials();
        std::vector<ShaderGen::MaterialData> gpuMaterials;

        for (const auto& mat : modelMaterials) {
            ShaderGen::MaterialData m{};
            m.baseColorFactor = mat.baseColorFactor;
            m.emissiveFactor = glm::vec4(0.0f);
            if (mat.baseColorTexture != INVALID_TEXTURE_HANDLE) {
                auto handle = m_renderer->device()->registerBindlessTexture(m_renderer->getTexture(mat.baseColorTexture), m_dummySampler.get());
                m.baseColorTexture = handle.index;
            } else {
                m.baseColorTexture = 0xFFFFFFFF;
            }
            m.normalTexture = 0xFFFFFFFF;
            m.metallicRoughnessTexture = 0xFFFFFFFF;
            m.emissiveTexture = 0xFFFFFFFF;
            m.metallicFactor = 1.0f;
            m.roughnessFactor = 1.0f;
            m.alphaCutoff = 0.5f;
            gpuMaterials.push_back(m);
        }

        if (gpuMaterials.empty()) gpuMaterials.push_back({});

        size_t size = gpuMaterials.size() * sizeof(ShaderGen::MaterialData);
        m_materialBuffer = m_renderer->device()->createBuffer(
            size,
            renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::TransferDst,
            renderer::rhi::MemoryUsage::GPUOnly
        );

        auto staging = m_renderer->device()->createBuffer(size, renderer::rhi::BufferUsage::TransferSrc, renderer::rhi::MemoryUsage::CPUToGPU);
        staging->uploadData(gpuMaterials.data(), size);

        auto cmd = m_renderer->device()->createCommandBuffer();
        cmd->begin();
        cmd->copyBuffer(staging.get(), m_materialBuffer.get(), 0, 0, size);
        cmd->end();
        m_renderer->device()->submitCommands(cmd.get());
        m_renderer->device()->waitIdle();
    }

    void createPipeline() {
        // Configure reflection for bindless resources
        renderer::rhi::ReflectionConfig config;
        // The default config already includes bindlessTextures with 100,000 count
        // which matches our bindless descriptor set size

        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("gltf_bindless.vert.spv"), config);
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("gltf_bindless.frag.spv"), config);

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

    void createDescriptors() {
        auto* pipeline = m_renderer->pipeline(m_pipeline);
        if (!pipeline) {
            throw std::runtime_error("Pipeline is null");
        }

        auto* materialLayout = pipeline->descriptorSetLayout(0);
        if (!materialLayout) {
            throw std::runtime_error("Pipeline descriptor set layouts are missing");
        }

        m_materialSet = m_renderer->device()->allocateDescriptorSet(materialLayout);
        m_materialSet->updateBuffer(0, m_materialBuffer.get(), 0, m_materialBuffer->size());
    }

    void recordFrame(const renderer::RHIFrameContext& ctx) {
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);
        m_renderer->bindDescriptorSet(ctx.commandBuffer, m_pipeline, 0, m_materialSet.get());
        
        void* nativeSet = m_renderer->device()->getBindlessDescriptorSetNative();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, nativeSet);

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
                    renderer::rhi::ShaderStage::Vertex,
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

    void onShutdown() override {}
};

int main(int argc, char** argv) {
    RHIGltfBindlessApp app;
    return app.run();
}