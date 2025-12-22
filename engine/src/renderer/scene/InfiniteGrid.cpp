#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_shader.hpp" // Use the RHI shader abstraction
#include <glm/gtc/matrix_transform.hpp>

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "generated/grid.vert.h"

namespace pnkr::renderer::scene
{
    void InfiniteGrid::init(RHIRenderer& renderer)
    {
        m_renderer = &renderer;

        // 2. Create the pipeline immediately
        createGridPipeline();

        core::Logger::info("Infinite grid initialized.");
    }

    void InfiniteGrid::destroy()
    {
        // In a real engine, you'd release the TextureHandle and PipelineHandle
        // back to the renderer here.
        m_pipeline = INVALID_PIPELINE_HANDLE;
    }

    void InfiniteGrid::createGridPipeline()
    {
        // 1. Load Shaders using the RHI Shader abstraction
        // This handles reflection automatically
        auto vertShader = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/grid.vert.spv");
        auto fragShader = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/grid.frag.spv");

        if (!vertShader || !fragShader)
        {
            core::Logger::error("Failed to load grid shaders");
            return;
        }

        // 2. Configure Pipeline
        // We use the RHI Pipeline Builder helper to make this clean
        rhi::RHIPipelineBuilder builder;

        builder.setShaders(vertShader.get(), fragShader.get(), nullptr)
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setPolygonMode(rhi::PolygonMode::Fill)
               .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
               .setAlphaBlend()
               .setCullMode(rhi::CullMode::None)
               .setColorFormat(m_renderer->getDrawColorFormat())
               .setDepthFormat(m_renderer->getDrawDepthFormat());


        // 3. Build
        // The builder automatically merges the Bindless Layout from the shader reflection
        // provided your shaders utilize the bindless sets (set=1).
        auto desc = builder.buildGraphics();

        // Ensure depth format is set explicitly if the builder didn't do it
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void InfiniteGrid::draw(rhi::RHICommandBuffer* cmd, const Camera& camera) const
    {
        if (!m_pipeline || (m_renderer == nullptr))
        {
            return;
        }

        // 1. Get underlying RHI objects
        rhi::RHIPipeline* rhiPipe = m_renderer->getPipeline(m_pipeline);
        if (rhiPipe == nullptr)
        {
            return;
        }

        cmd->bindPipeline(rhiPipe);


        // 4. Push Constants
        ShaderGen::GridPushConstants pc{};
        pc.MVP = glm::perspective(glm::radians(45.0f), 16/9.0f, 0.1f, 1000.0f) * camera.view(); // Remove translation
        pc.cameraPos = glm::vec4(camera.position(), 1);
        pc.origin = glm::vec4(0);
        cmd->pushConstants(rhiPipe,
                           rhi::ShaderStage::Vertex,
                           0,
                           sizeof(pc),
                           &pc);

        // 5. Draw
        cmd->draw(6, 1, 0, 0);
    }
} // namespace pnkr::renderer::scene
