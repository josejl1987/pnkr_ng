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
        : samples::RhiSampleApp({"RHI Cube", 800, 600, SDL_WINDOW_RESIZABLE, false})
    {
    }

    renderer::scene::Camera m_camera;

    void onInit() override
    {
        // 1. Init Renderer
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        // 2. Setup Camera
        m_camera.lookAt({0.0f, 2.0f, 4.0f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        float aspect = (float)m_window.width() / (float)m_window.height();
        m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        // 3. Create Geometry
        auto cubeData = samples::GeometryUtils::getCube();
        m_cubeMesh = m_renderer->createMesh(cubeData.vertices, cubeData.indices);

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
            { 0, sizeof(renderer::Vertex), renderer::rhi::VertexInputRate::Vertex }
        };

        std::vector<renderer::rhi::VertexInputAttribute> attribs = {
            {0, 0, renderer::rhi::Format::R32G32B32_SFLOAT, offsetof(renderer::Vertex, m_position)},
            {1, 0, renderer::rhi::Format::R32G32B32_SFLOAT, offsetof(renderer::Vertex, m_color)},
            {2, 0, renderer::rhi::Format::R32G32B32_SFLOAT, offsetof(renderer::Vertex, m_normal)},
            {3, 0, renderer::rhi::Format::R32G32_SFLOAT,    offsetof(renderer::Vertex, m_texCoord)}
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
            .setShaders(vs.get(), fs.get())
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back)
            .useVertexType<renderer::Vertex>()
            .enableDepthTest(true)
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .buildGraphics();

        // FIX: Validation Error 06055
        // Explicitly add a blend attachment to match the color attachment count.
        renderer::rhi::BlendAttachment blend{};
        blend.blendEnable = false;
        // Assuming default constructor sets valid write mask (e.g. RGBA), or 0 if omitted.
        // Explicitly setting it would be safer if we knew the Enum, but default init is used in rhiTriangle.
        desc.blend.attachments.push_back(blend);

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        static float timeVal = 0.0f;
        timeVal += ctx.deltaTime;

        // 1. Calculate Transform
        renderer::scene::Transform xform;
        xform.m_rotation = glm::angleAxis(timeVal, glm::vec3{0.0f, 1.0f, 0.0f});

        // 2. Prepare Data
        ShaderGen::PushConstants pc{};
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
                m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
            }
        }
    }

private:
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    MeshHandle m_cubeMesh;
    PipelineHandle m_pipeline;

    // Helper to load SPIR-V
    std::vector<uint32_t> loadSpirv(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open SPIR-V file: " + filename);
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