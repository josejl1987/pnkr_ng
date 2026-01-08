#include "pnkr/engine.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <imgui.h>

using namespace pnkr;

const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

const std::vector<renderer::Vertex> kVertices = []()
{
    std::vector<renderer::Vertex> v;
    const glm::vec3 pos[] = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}
    };
    for (const auto& p : pos)
    {
        renderer::Vertex vert{};
        vert.position = {p.x, p.y, p.z, 0};
        v.push_back(vert);
    }
    return v;
}();

const std::vector<uint32_t> kIndices = {
    0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
    1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
    3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
    4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1
};

const glm::vec3 K_COLORS[10] = {
    {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
    {1, 1, 0}, {0, 1, 1}, {1, 0, 1},
    {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}
};

struct PushConstants
{
    glm::mat4 mvp;
    uint32_t texture;
};

class OffscreenMipDemo : public app::Application
{
public:
    OffscreenMipDemo() : Application({
        .title = "Offscreen Mip Rendering", .width = 1280, .height = 720, .createRenderer = false, .rendererConfig = {}
    })
    {
    }

    MeshHandle m_mesh;
    TextureHandle m_texture;
    std::vector<TextureHandle> m_mipViews;
    PipelineHandle m_pipeline = INVALID_PIPELINE_HANDLE;
    float m_rotation = 0.0f;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;

    void onInit() override
    {
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        m_mesh = m_renderer->createMesh(kVertices, kIndices, false);

        renderer::rhi::TextureDescriptor texDesc{};
        texDesc.extent = {512, 512, 1};
        texDesc.format = renderer::rhi::Format::R8G8B8A8_UNORM;
        texDesc.usage = renderer::rhi::TextureUsage::ColorAttachment | renderer::rhi::TextureUsage::Sampled;
        texDesc.mipLevels = 10;
        texDesc.debugName = "MipColorTexture";
        m_texture = m_renderer->createTexture(texDesc);

        auto cmd = m_renderer->device()->createCommandList();
        cmd->begin();

        for (uint32_t i = 0; i < texDesc.mipLevels; ++i)
        {
            renderer::rhi::TextureViewDescriptor viewDesc{};
            viewDesc.mipLevel = i;
            viewDesc.mipCount = 1;
            viewDesc.layerCount = 1;

            TextureHandle viewHandle = m_renderer->createTextureView(m_texture, viewDesc);
            m_mipViews.push_back(viewHandle);

            auto* rhiView = m_renderer->getTexture(viewHandle);

            renderer::rhi::RHIMemoryBarrier barrier{};
            barrier.texture = rhiView;
            barrier.oldLayout = renderer::rhi::ResourceLayout::Undefined;
            barrier.newLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            barrier.srcAccessStage = renderer::rhi::ShaderStage::None;
            barrier.dstAccessStage = renderer::rhi::ShaderStage::RenderTarget;

            cmd->pipelineBarrier(
                renderer::rhi::ShaderStage::None,
                renderer::rhi::ShaderStage::RenderTarget,
                {barrier}
            );

            renderer::rhi::RenderingInfo info{};
            info.renderArea = {0, 0, (uint32_t)rhiView->extent().width, (uint32_t)rhiView->extent().height};

            renderer::rhi::RenderingAttachment att{};
            att.texture = rhiView;
            att.loadOp = renderer::rhi::LoadOp::Clear;
            att.storeOp = renderer::rhi::StoreOp::Store;
            att.clearValue.isDepthStencil = false;

            const auto& col = K_COLORS[i % 10];
            att.clearValue.color.float32[0] = col.r;
            att.clearValue.color.float32[1] = col.g;
            att.clearValue.color.float32[2] = col.b;
            att.clearValue.color.float32[3] = 1.0f;

            info.colorAttachments.push_back(att);

            cmd->beginRendering(info);
            cmd->endRendering();

            barrier.oldLayout = renderer::rhi::ResourceLayout::ColorAttachment;
            barrier.newLayout = renderer::rhi::ResourceLayout::ShaderReadOnly;
            barrier.srcAccessStage = renderer::rhi::ShaderStage::RenderTarget;
            barrier.dstAccessStage = renderer::rhi::ShaderStage::Fragment;

            cmd->pipelineBarrier(
                renderer::rhi::ShaderStage::RenderTarget,
                renderer::rhi::ShaderStage::Fragment,
                {barrier}
            );
        }

        cmd->end();
        m_renderer->device()->submitCommands(cmd.get());
        m_renderer->device()->waitIdle();

        createPipeline();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->onRecord(ctx);
        });
    }

    void createPipeline()
    {
        auto vert = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, "shaders/offscreen.vert.spv");
        auto frag = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, "shaders/offscreen.frag.spv");

        if (!vert || !frag)
        {
            pnkr::core::Logger::error("Failed to load offscreen shaders");
            return;
        }

        renderer::rhi::RHIPipelineBuilder builder;
        builder.setShaders(vert.get(), frag.get())
               .useVertexType<renderer::Vertex>()
               .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
               .setColorFormat(m_renderer->getSwapchainColorFormat())
               .setDepthFormat(m_renderer->getDrawDepthFormat())
               .enableDepthTest(true, renderer::rhi::CompareOp::Less)
               .setName("OffscreenIcoPipeline");

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void onUpdate(float dt) override
    {
        m_rotation += dt * 0.5f;
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        if (!m_pipeline) return;

        ctx.commandBuffer->bindPipeline(m_renderer->getPipeline(m_pipeline));
        auto meshView = m_renderer->getMeshView(m_mesh);
        if (!meshView) return;
        if (!meshView->vertexPulling)
        {
            ctx.commandBuffer->bindVertexBuffer(0, meshView->vertexBuffer, 0);
        }
        ctx.commandBuffer->bindIndexBuffer(meshView->indexBuffer, 0, false);
        const float t = (1 + sqrtf(5)) / 2;
        glm::mat4 view = glm::lookAt<float>(glm::vec3(0.0f, 3.0f, -4.5f), glm::vec3(0.0f, t, 0.0f),
                                            glm::vec3(0.0f, 1.f, 0.f));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)m_config.width / m_config.height, 0.1f, 100.0f);
        proj[1][1] *= -1.0f;
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), m_rotation, glm::vec3(0, 1, 0));
        model = glm::rotate(model, m_rotation * 0.5f, glm::vec3(1, 0, 0));

        PushConstants pc{};
        pc.mvp = proj * view * model;
        pc.texture = util::u32(m_renderer->getTextureBindlessIndex(m_texture));

        ctx.commandBuffer->pushConstants(
            renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment,
            pc);

        ctx.commandBuffer->drawIndexed(meshView->indexCount, 1, 0, 0, 0);
    }

    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }

    void onImGui() override
    {
        ImGui::Begin("Texture Views");
        ImGui::Text("Full Texture (Trilinear)");
        ImGui::Image((ImTextureID)(uintptr_t)util::u32(m_renderer->getTextureBindlessIndex(m_texture)), ImVec2(128, 128));

        ImGui::Text("Individual Mips:");
        for (size_t i = 0; i < m_mipViews.size(); ++i)
        {
            if (i > 0 && (i % 5 != 0)) ImGui::SameLine();
            uint32_t id = util::u32(m_renderer->getTextureBindlessIndex(m_mipViews[i]));

            float s = std::max(4.0f, 64.0f / (float)(1 << i) * 2.0f);
            ImGui::Image((ImTextureID)(uintptr_t)id, ImVec2(s, s));
        }
        ImGui::End();
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }
};

int main(int, char**)
{
    OffscreenMipDemo app;
    return app.run();
}
