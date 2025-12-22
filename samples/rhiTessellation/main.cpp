#include <imgui.h>

#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/ui/imgui_layer.hpp"

#include "generated/gltf_bindless.frag.h"

#include "../common/RhiSampleApp.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <variant>


using namespace pnkr;

class RHITessellationApp : public samples::RhiSampleApp
{
public:
    RHITessellationApp()
        : samples::RhiSampleApp({
            .title = "RHI Tessellation GLTF",
            .width = 1280,
            .height = 720,
            .windowFlags = SDL_WINDOW_RESIZABLE,
            .createRenderer = false
        })
    {
    }

    renderer::scene::Camera m_camera;
    renderer::scene::CameraController m_cameraController;
    std::unique_ptr<renderer::scene::Model> m_model;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    PipelineHandle m_pipeline;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_materialBuffer;
    std::unique_ptr<renderer::rhi::RHISampler> m_dummySampler;
    pnkr::ui::ImGuiLayer m_imgui;
    float m_tessScale = 1.0F;

    void onInit() override
    {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        m_imgui.init(m_renderer.get(), &m_window);

        m_cameraController.setPosition({2.0F, 2.0F, 2.0F});
        m_cameraController.applyToCamera(m_camera);

        m_model = renderer::scene::Model::load(*m_renderer, baseDir() / "assets" / "Duck.glb", true);
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

    void onShutdown() override
    {
        m_imgui.shutdown();
    }

    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }

    void onUpdate(float deltaTime) override
    {
        m_cameraController.update(m_input, deltaTime);
        m_cameraController.applyToCamera(m_camera);

        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);

        m_imgui.beginFrame();
        if (ImGui::Begin("Tessellation Controls"))
        {
            ImGui::SliderFloat("Tess Scale", &m_tessScale, 1.0f, 64.0f);
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        }
        ImGui::End();
        m_imgui.endFrame();
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
        stagingDesc.size = size;
        stagingDesc.usage = renderer::rhi::BufferUsage::TransferSrc;
        stagingDesc.memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU;
        stagingDesc.data = gpuMaterials.data();

        auto staging = m_renderer->device()->createBuffer(stagingDesc);

        auto cmd = m_renderer->device()->createCommandBuffer();
        cmd->begin();
        cmd->copyBuffer(staging.get(), m_materialBuffer.get(), 0, 0, size);
        cmd->end();
        m_renderer->device()->submitCommands(cmd.get());
        m_renderer->device()->waitIdle();
    }

    void createPipeline()
    {
        renderer::rhi::ReflectionConfig config;

        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex,
                                              getShaderPath("tessellation.vert.spv"), config);
        auto tcs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::TessControl,
                                               getShaderPath("tessellation.tesc.spv"), config);
        auto tes = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::TessEval,
                                               getShaderPath("tessellation.tese.spv"), config);
        auto gs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Geometry,
                                              getShaderPath("wireframe.geom.spv"), config);
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment,
                                              getShaderPath("gltf_bindless.frag.spv"), config);

        auto builder = renderer::rhi::RHIPipelineBuilder()
                       .setShaders(vs.get(), fs.get(), tcs.get(), tes.get(), gs.get())
                       .setTopology(renderer::rhi::PrimitiveTopology::PatchList)
                       .setPatchControlPoints(3)
                       .setCullMode(renderer::rhi::CullMode::Back)
                       .enableDepthTest()
                       .setColorFormat(m_renderer->getDrawColorFormat())
                       .setDepthFormat(m_renderer->getDrawDepthFormat())
                       .setName("GltfTessellation");

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, bindlessSet);

        std::function<void(int)> drawNode = [&](int nodeIdx)
        {
            const auto& node = m_model->nodes()[nodeIdx];

            for (const auto& prim : node.m_meshPrimitives)
            {
                ShaderGen::gltf_bindless_frag_PushConstants pc{};
                pc.model = node.m_worldTransform.mat4();
                pc.viewProj = m_camera.viewProj();
                pc.cameraPos = glm::vec4(m_cameraController.position(), 1.0F);
                pc.tessScale = m_tessScale;
                pc.materialIndex = prim.m_materialIndex;
                pc.vtx = prim.m_vertexBufferAddress;
                pc.materialBuffer = m_materialBuffer->getDeviceAddress();

                const auto stages = renderer::rhi::ShaderStage::Vertex |
                    renderer::rhi::ShaderStage::TessControl |
                    renderer::rhi::ShaderStage::TessEval |
                    renderer::rhi::ShaderStage::Geometry |
                    renderer::rhi::ShaderStage::Fragment;
                m_renderer->pushConstants(ctx.commandBuffer, m_pipeline, stages, pc);

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

        m_imgui.render(ctx.commandBuffer);
    }

    void onEvent(const SDL_Event& event) override
    {
        m_imgui.handleEvent(event);
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }
};

int main(int argc, char** argv)
{
    RHITessellationApp app;
    return app.run();
}