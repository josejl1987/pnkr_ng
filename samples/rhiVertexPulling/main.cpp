#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"

#include "generated/vertex_pulling.vert.h"

#include "../common/RhiSampleApp.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <variant>

using namespace pnkr;

class RHIVertexPullingApp : public samples::RhiSampleApp
{
public:
    RHIVertexPullingApp()
        : samples::RhiSampleApp({
            .title = "RHI Vertex pulling GLTF", .width = 1280, .height = 720, .windowFlags = SDL_WINDOW_RESIZABLE,
            .createRenderer = false
        })
    {
    }

    renderer::scene::Camera m_camera;
    std::unique_ptr<renderer::scene::Model> m_model;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    PipelineHandle m_pipeline;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_materialBuffer;
    std::unique_ptr<renderer::rhi::RHITexture> m_dummyTexture;
    std::unique_ptr<renderer::rhi::RHISampler> m_dummySampler;

    void onInit() override
    {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        m_camera.lookAt({2.0F, 2.0F, 2.0F}, {0.F, 0.5F, 0.F}, {0.F, 1.F, 0.F});

        m_model = renderer::scene::Model::load(*m_renderer, baseDir() / "assets" / "duck.glb", true);


        if (!m_model)
        {
            throw cpptrace::runtime_error("Failed to load model");
        }

        if (!m_dummySampler)
        {
            m_dummySampler = m_renderer->device()->createSampler(
                renderer::rhi::Filter::Linear,
                renderer::rhi::Filter::Linear,
                renderer::rhi::SamplerAddressMode::Repeat
            );
        }
        uploadMaterials();
        createPipeline();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            recordFrame(ctx);
        });
    }

    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }

    void uploadMaterials()
    {
        const auto& modelMaterials = m_model->materials();
        std::vector<ShaderGen::MaterialData> gpuMaterials;

        for (const auto& mat : modelMaterials)
        {
            ShaderGen::MaterialData m{};
            m.baseColorFactor = mat.m_baseColorFactor;
            m.emissiveFactor = glm::vec4(0.0F);
            if (mat.m_baseColorTexture != INVALID_TEXTURE_HANDLE)
            {
                auto handle = m_renderer->device()->registerBindlessTexture(
                    m_renderer->getTexture(mat.m_baseColorTexture), m_dummySampler.get());
                m.baseColorTexture = handle.index;
            }
            else
            {
                m.baseColorTexture = 0xFFFFFFFF;
            }
            m.normalTexture = 0xFFFFFFFF;
            m.metallicRoughnessTexture = 0xFFFFFFFF;
            m.emissiveTexture = 0xFFFFFFFF;
            m.metallicFactor = 1.0F;
            m.roughnessFactor = 1.0F;
            m.alphaCutoff = 0.5F;
            gpuMaterials.push_back(m);
        }

        if (gpuMaterials.empty())
        {
            gpuMaterials.push_back({});
        }

                size_t size = gpuMaterials.size() * sizeof(ShaderGen::MaterialData);

                renderer::rhi::BufferDescriptor bufferDesc;

                bufferDesc.size = size;

                bufferDesc.usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::TransferDst | renderer::rhi::BufferUsage::ShaderDeviceAddress;

                bufferDesc.memoryUsage = renderer::rhi::MemoryUsage::GPUOnly;

                m_materialBuffer = m_renderer->device()->createBuffer(bufferDesc);

        

                renderer::rhi::BufferDescriptor stagingDesc;

        // ... (omitting some lines)

            void recordFrame(const renderer::RHIFrameContext& ctx)

            {

                m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        

                renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();

                ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, bindlessSet);

        

                float aspect = (float)m_window.width() / m_window.height();

                m_camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);

        

                std::function<void(int)> drawNode = [&](int nodeIdx)

                {

                    const auto& node = m_model->nodes()[nodeIdx];

        

                    for (const auto& prim : node.m_meshPrimitives)

                    {

                        ShaderGen::vertex_pulling_vert_PushConstants pc{};

                        pc.model = node.m_worldTransform.mat4();

                        pc.viewProj = m_camera.viewProj();

                        pc.materialIndex = prim.m_materialIndex;

                        pc.vtx = prim.m_vertexBufferAddress;

                        pc.materialBuffer = m_materialBuffer->getDeviceAddress();

                        m_renderer->pushConstants(ctx.commandBuffer, m_pipeline,

                                                  renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment,

                                                  pc);

        

                        m_renderer->bindMesh(ctx.commandBuffer, prim.m_mesh);

                        m_renderer->drawMesh(ctx.commandBuffer, prim.m_mesh);

                    }

        

            for (int child : node.m_children)
            {
                drawNode(child);
            }
        };

        for (int root : m_model->rootNodes())
        {
            drawNode(root);
        }
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }


    void onShutdown() override
    {
    }
};


int main(int argc, char** argv)
{
    RHIVertexPullingApp app;
    return app.run();
}
