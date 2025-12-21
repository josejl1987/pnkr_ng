#include "generated/cube.vert.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <vector>

// Assuming these exist in your common folder as per your snippet
#include "../common/GeometryUtils.h"
#include "../common/RhiSampleApp.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"

using namespace pnkr;

class RHICubeApp : public samples::RhiSampleApp
{
public:
    RHICubeApp()
        : samples::RhiSampleApp({.title="RHI Cube", .width=800, .height=600, .windowFlags=SDL_WINDOW_RESIZABLE, .createRenderer=false})
    {
    }

    renderer::scene::Camera m_camera;

    void onInit() override
    {
        // 1. Init Renderer
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        // 2. Setup Camera
        m_camera.lookAt({0.0F, 2.0F, 4.0F}, {0.F, 0.F, 0.F}, {0.F, 1.F, 0.F});
        float aspect = (float)m_window.width() / (float)m_window.height();
        m_camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);

        // 3. Create Geometry
        auto cubeData = samples::GeometryUtils::getCube();
        m_cubeMesh = m_renderer->createMesh(cubeData.vertices, cubeData.indices, false);

        // 4. Create Pipeline
        createPipeline();

        // 5. Set Record Callback
        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void createPipeline()
    {
        // Define Vertex Input manually for RHI
        std::vector<renderer::rhi::VertexInputBinding> bindings = {
            { .binding=0, .stride=sizeof(renderer::Vertex), .inputRate=renderer::rhi::VertexInputRate::Vertex }
        };

        std::vector<renderer::rhi::VertexInputAttribute> attribs = {
            {.location=0, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_position)},
            {.location=1, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_color)},
            {.location=2, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_normal)},
            {.location=3, .binding=0, .format=renderer::rhi::Format::R32G32_SFLOAT,    .offset=offsetof(renderer::Vertex, m_texCoord)}
        };

        auto vs = renderer::rhi::Shader::load(
            renderer::rhi::ShaderStage::Vertex,
            getShaderPath("cube.vert.spv")
        );

        auto fs = renderer::rhi::Shader::load(
            renderer::rhi::ShaderStage::Fragment,
            getShaderPath("cube.frag.spv")
        );

        // Builder merges reflection data automatically
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

        // 1. Calculate Transform
        renderer::scene::Transform xform;
        xform.m_rotation = glm::angleAxis(timeVal, glm::vec3{0.0F, 1.0F, 0.0F});

        // 2. Prepare Data
        ShaderGen::cube_vert_PushConstants pc{};
        pc.model = xform.mat4();
        pc.viewProj = m_camera.viewProj();

        // 3. Bind Pipeline
        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);

        // 4. Push Constants
        m_renderer->pushConstants(
            ctx.commandBuffer,
            m_pipeline,
            renderer::rhi::ShaderStage::Vertex,
            pc
        );

        // 5. Draw
        m_renderer->bindMesh(ctx.commandBuffer, m_cubeMesh);
        m_renderer->drawMesh(ctx.commandBuffer, m_cubeMesh);
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

    // Helper to load SPIR-V
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