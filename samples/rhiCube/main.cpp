#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <vector>

struct PushConstants {
    glm::mat4 model;
    glm::mat4 viewProj;
};

#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/app/Application.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"

using namespace pnkr;

class RHICubeApp : public app::Application
{
public:
    RHICubeApp()
        : app::Application({.title="RHI Cube", .width=800, .height=600, .windowFlags=SDL_WINDOW_RESIZABLE, .createRenderer=false})
    {
    }

    renderer::scene::Camera m_camera;

    void onInit() override
    {
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        m_camera.setPerspective(glm::radians(45.0f), (float)m_config.width / m_config.height, 0.1f, 1000.0f);
        m_camera.lookAt({2.0f, 2.0f, 2.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});

        auto cubeData = renderer::geometry::GeometryUtils::getCube(1.0f);
        m_cubeMesh = m_renderer->createMesh(cubeData.vertices, cubeData.indices, false);

        createPipeline();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void createPipeline()
    {

        std::vector<renderer::rhi::VertexInputBinding> bindings = {
            { .binding=0, .stride=sizeof(renderer::Vertex), .inputRate=renderer::rhi::VertexInputRate::Vertex }
        };

        std::vector<renderer::rhi::VertexInputAttribute> attribs = {
            {.location=0, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, position), .semantic=renderer::rhi::VertexSemantic::Position},
            {.location=1, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, color), .semantic=renderer::rhi::VertexSemantic::Color},
            {.location=2, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, normal), .semantic=renderer::rhi::VertexSemantic::Normal},
            {.location=3, .binding=0, .format=renderer::rhi::Format::R32G32_SFLOAT,    .offset=offsetof(renderer::Vertex, uv0), .semantic=renderer::rhi::VertexSemantic::TexCoord0},
            {.location=4, .binding=0, .format=renderer::rhi::Format::R32G32_SFLOAT,    .offset=offsetof(renderer::Vertex, uv1), .semantic=renderer::rhi::VertexSemantic::TexCoord1}
        };

        auto vs = renderer::rhi::Shader::load(
            renderer::rhi::ShaderStage::Vertex,
            getShaderPath("cube.vert.spv")
        );

        auto fs = renderer::rhi::Shader::load(
            renderer::rhi::ShaderStage::Fragment,
            getShaderPath("cube.frag.spv")
        );

        auto desc = renderer::rhi::RHIPipelineBuilder()
            .setName("CubePipeline")
            .setShaders(vs.get(), fs.get(), nullptr)
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back, true)
            .useVertexType<renderer::Vertex>()
            .enableDepthTest(true)
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .buildGraphics();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        static float timeVal = 0.0F;
        timeVal += ctx.deltaTime;

        renderer::scene::Transform xform;
        xform.m_rotation = glm::angleAxis(timeVal, glm::vec3{0.0F, 1.0F, 0.0F});

        PushConstants pc{};
        pc.model = xform.mat4();
        pc.viewProj = m_camera.viewProj();

        ctx.commandBuffer->bindPipeline(m_renderer->getPipeline(m_pipeline));

        ctx.commandBuffer->pushConstants(renderer::rhi::ShaderStage::Vertex, pc);

        auto meshView = m_renderer->getMeshView(m_cubeMesh);
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

            if (event.window.data2 > 0) {
                float aspect = (float)event.window.data1 / (float)event.window.data2;
                m_camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);
            }
        }
    }

private:
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    MeshHandle m_cubeMesh;
    PipelineHandle m_pipeline;

    static std::vector<uint32_t> loadSpirv(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
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
    (void)argc; (void)argv;
    RHICubeApp app;
    return app.run();
}

