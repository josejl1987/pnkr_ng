#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"

// CRITICAL: Use the generated header
#include "generated/cube.vert.h"
// Common utilities
#include "../common/GeometryUtils.h"
#include "../common/RhiSampleApp.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <vector>

using namespace pnkr;

class RHIMultiDrawApp : public samples::RhiSampleApp
{
public:
    RHIMultiDrawApp()
        : samples::RhiSampleApp({"PNKR - RHI MultiDraw", 800, 600, SDL_WINDOW_RESIZABLE, false})
    {
    }

    // Scene Data
    renderer::scene::Camera m_camera;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;

    // Resources
    MeshHandle m_cubeMesh;
    MeshHandle m_planeMesh;
    PipelineHandle m_cubePipeline;
    PipelineHandle m_planePipeline;

    void onInit() override
    {
        // 1. Init Renderer
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        // 2. Setup Camera
        m_camera.lookAt({1.5f, 2.0f, 2.5f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
        updatePerspective();

        // 3. Create Geometry
        auto cubeData = samples::GeometryUtils::getCube();
        m_cubeMesh = m_renderer->createMesh(cubeData.vertices, cubeData.indices);

        auto planeData = samples::GeometryUtils::getPlane(2.5f, -0.6f);
        m_planeMesh = m_renderer->createMesh(planeData.vertices, planeData.indices);

        // 4. Create Pipelines
        createPipelines();

        // 5. Set Record Callback
        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void createPipelines()
    {
        // Load Shaders
        auto vertSpirv = loadSpirv(getShaderPath("cube.vert.spv").string());
        auto fragCubeSpirv = loadSpirv(getShaderPath("cube.frag.spv").string());
        auto fragPlaneSpirv = loadSpirv(getShaderPath("plane_tint.frag.spv").string());

        // --- Base Descriptor (Shared Config) ---
        renderer::rhi::GraphicsPipelineDescriptor desc{};
        desc.debugName = "CubePipeline";

        // Vertex Input
        desc.vertexBindings.push_back({ 0, sizeof(renderer::Vertex), renderer::rhi::VertexInputRate::Vertex });
        desc.vertexAttributes = {
            {.location=0, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_position)},
            {.location=1, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_color)},
            {.location=2, .binding=0, .format=renderer::rhi::Format::R32G32B32_SFLOAT, .offset=offsetof(renderer::Vertex, m_normal)},
            {.location=3, .binding=0, .format=renderer::rhi::Format::R32G32_SFLOAT,    .offset=offsetof(renderer::Vertex, m_texCoord)}
        };

        // Standard State
        desc.topology = renderer::rhi::PrimitiveTopology::TriangleList;
        desc.rasterization.polygonMode = renderer::rhi::PolygonMode::Fill;
        desc.rasterization.cullMode = renderer::rhi::CullMode::Back;
        desc.rasterization.frontFaceCCW = true;
        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = true;
        desc.depthStencil.depthCompareOp = renderer::rhi::CompareOp::Less;

        // Output Formats
        desc.colorFormats.push_back(m_renderer->getDrawColorFormat());
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        // FIX: Add Blend Attachment to match colorFormats[0]
        renderer::rhi::BlendAttachment blend{};
        blend.blendEnable = false;
        desc.blend.attachments.push_back(blend);

        // Push Constants
        desc.pushConstants.push_back({
            renderer::rhi::ShaderStage::Vertex,
            0,
            sizeof(ShaderGen::PushConstants)
        });

        // --- Pipeline 1: Cube ---
        desc.shaders = {
            { renderer::rhi::ShaderStage::Vertex, vertSpirv, "main" },
            { renderer::rhi::ShaderStage::Fragment, fragCubeSpirv, "main" }
        };
        m_cubePipeline = m_renderer->createGraphicsPipeline(desc);

        // --- Pipeline 2: Plane ---
        desc.debugName = "PlanePipeline";
        desc.shaders = {
            { renderer::rhi::ShaderStage::Vertex, vertSpirv, "main" },
            { renderer::rhi::ShaderStage::Fragment, fragPlaneSpirv, "main" }
        };
        m_planePipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        // Update Animation
        m_timeAccumulator += ctx.deltaTime;

        // Shared ViewProj
        ShaderGen::PushConstants pc{};
        pc.viewProj = m_camera.viewProj();

        // --- Draw Cube ---
        {
            renderer::scene::Transform cubeXform;
            cubeXform.m_rotation = glm::angleAxis(m_timeAccumulator, glm::vec3(0.0f, 1.0f, 0.0f));
            pc.model = cubeXform.mat4();

            m_renderer->bindPipeline(ctx.commandBuffer, m_cubePipeline);
            m_renderer->pushConstants(ctx.commandBuffer, m_cubePipeline, renderer::rhi::ShaderStage::Vertex, pc);
            m_renderer->bindMesh(ctx.commandBuffer, m_cubeMesh);
            m_renderer->drawMesh(ctx.commandBuffer, m_cubeMesh);
        }

        // --- Draw Plane ---
        {
            renderer::scene::Transform planeXform;
            pc.model = planeXform.mat4();

            m_renderer->bindPipeline(ctx.commandBuffer, m_planePipeline);
            m_renderer->pushConstants(ctx.commandBuffer, m_planePipeline, renderer::rhi::ShaderStage::Vertex, pc);
            m_renderer->bindMesh(ctx.commandBuffer, m_planeMesh);
            m_renderer->drawMesh(ctx.commandBuffer, m_planeMesh);
        }
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
            if (event.window.data2 > 0) {
                m_renderer->resize(event.window.data1, event.window.data2);
                updatePerspective();
            }
        }
    }

private:
    float m_timeAccumulator = 0.0f;

    void updatePerspective() {
        if (m_window.height() > 0) {
            float aspect = (float)m_window.width() / (float)m_window.height();
            m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        }
    }

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
    RHIMultiDrawApp app;
    return app.run();
}