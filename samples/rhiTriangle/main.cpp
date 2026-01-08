#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/core/Handle.h"
#include <glm/glm.hpp>
#include <fstream>
#include <vector>

#include "pnkr/app/Application.hpp"

using namespace pnkr;

class RHITriangleApp : public app::Application
{
public:

    RHITriangleApp()
        : app::Application({.title="RHI Triangle", .width=800, .height=600, .windowFlags=SDL_WINDOW_RESIZABLE, .createRenderer=false})
    {
    }

    void onInit() override
    {

        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        std::vector<renderer::Vertex> vertices = {
            {.position={-0.5F, -0.5F, 0.0F, 1.0F}, .color={1.0F, 0.0F, 0.0F, 1.0F}, .normal={0.0F, 0.0F, 1.0F, 0.0F}, .uv0={0.0F, 0.0F}, .uv1={0.0F, 0.0F}, .tangent={0.0F, 0.0F, 0.0F, 0.0F}, .joints={0, 0, 0, 0}, .weights={1.0f, 0.0f, 0.0f, 0.0f}},
            {.position={0.5F, -0.5F, 0.0F, 1.0F}, .color={0.0F, 1.0F, 0.0F, 1.0F}, .normal={0.0F, 0.0F, 1.0F, 0.0F}, .uv0={1.0F, 0.0F}, .uv1={0.0F, 0.0F}, .tangent={0.0F, 0.0F, 0.0F, 0.0F}, .joints={0, 0, 0, 0}, .weights={1.0f, 0.0f, 0.0f, 0.0f}},
            {.position={0.0F, 0.5F, 0.0F, 1.0F}, .color={0.0F, 0.0F, 1.0F, 1.0F}, .normal={0.0F, 0.0F, 1.0F, 0.0F}, .uv0={0.5F, 1.0F}, .uv1={0.0F, 0.0F}, .tangent={0.0F, 0.0F, 0.0F, 0.0F}, .joints={0, 0, 0, 0}, .weights={1.0f, 0.0f, 0.0f, 0.0f}}
        };

        std::vector<uint32_t> indices = {0, 1, 2};

        m_triangleMesh = m_renderer->createMesh(vertices, indices, false);

        createPipeline();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void createPipeline()
    {

        std::vector<uint32_t> vertSpirv = loadSpirv(getShaderPath("triangle.vert.spv").string());
        std::vector<uint32_t> fragSpirv = loadSpirv(getShaderPath("triangle.frag.spv").string());

        renderer::rhi::GraphicsPipelineDescriptor desc{};

        desc.shaders.push_back({
            .stage=renderer::rhi::ShaderStage::Vertex,
            .spirvCode=vertSpirv,
            .entryPoint="main"
        });
        desc.shaders.push_back({
            .stage=renderer::rhi::ShaderStage::Fragment,
            .spirvCode=fragSpirv,
            .entryPoint="main"
        });

        desc.vertexBindings.push_back({
            .binding=0,
            .stride=sizeof(renderer::Vertex),
            .inputRate=renderer::rhi::VertexInputRate::Vertex
        });

        desc.vertexAttributes = {
            {.location=0, .binding=0, .format=renderer::rhi::Format::R32G32B32A32_SFLOAT, .offset=offsetof(renderer::Vertex, position), .semantic=renderer::rhi::VertexSemantic::Position},
            {.location=1, .binding=0, .format=renderer::rhi::Format::R32G32B32A32_SFLOAT, .offset=offsetof(renderer::Vertex, color), .semantic=renderer::rhi::VertexSemantic::Color},
            {.location=2, .binding=0, .format=renderer::rhi::Format::R32G32B32A32_SFLOAT, .offset=offsetof(renderer::Vertex, normal), .semantic=renderer::rhi::VertexSemantic::Normal},
            {.location=3, .binding=0, .format=renderer::rhi::Format::R32G32_SFLOAT, .offset=offsetof(renderer::Vertex, uv0), .semantic=renderer::rhi::VertexSemantic::TexCoord0}
        };

        desc.topology = renderer::rhi::PrimitiveTopology::TriangleList;

        desc.rasterization.polygonMode = renderer::rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = renderer::rhi::CullMode::None;
        desc.rasterization.frontFaceCCW = true;

        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = true;
        desc.depthStencil.depthCompareOp = renderer::rhi::CompareOp::Less;

        renderer::rhi::BlendAttachment blendAttachment{};
        blendAttachment.blendEnable = false;
        desc.blend.attachments.push_back(blendAttachment);

        desc.colorFormats.push_back(m_renderer->getDrawColorFormat());
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        ctx.commandBuffer->bindPipeline(m_renderer->getPipeline(m_pipeline));
        auto meshView = m_renderer->getMeshView(m_triangleMesh);
        if (!meshView) return;
        if (!meshView->vertexPulling)
        {
            ctx.commandBuffer->bindVertexBuffer(0, meshView->vertexBuffer, 0);
        }
        ctx.commandBuffer->bindIndexBuffer(meshView->indexBuffer, 0, false);
        ctx.commandBuffer->drawIndexed(meshView->indexCount, 1, 0, 0, 0);
    }

    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }

private:
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    MeshHandle m_triangleMesh;
    PipelineHandle m_pipeline;

    static std::vector<uint32_t> loadSpirv(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw cpptrace::runtime_error("Failed to open SPIR-V file: " + filename);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        return buffer;
    }
};

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    RHITriangleApp app;
    return app.run();
}

