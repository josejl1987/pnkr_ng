#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/renderer/gpu_shared/GridShared.h"

namespace pnkr::renderer::scene
{
    void InfiniteGrid::init(RHIRenderer& renderer)
    {
        m_renderer = &renderer;

        createGridPipeline();

        core::Logger::Scene.info("Infinite grid initialized.");
    }

    void InfiniteGrid::destroy()
    {

        m_pipeline = {};
    }

    void InfiniteGrid::createGridPipeline()
    {
        using namespace passes::utils;
        auto shaders = loadGraphicsShaders("shaders/grid.vert.spv", "shaders/grid.frag.spv", "InfiniteGrid");
        if (!shaders.success)
        {
            return;
        }

        rhi::RHIPipelineBuilder builder;

        builder.setShaders(shaders.vertex.get(), shaders.fragment.get(), nullptr)
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setPolygonMode(rhi::PolygonMode::Fill)
               .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
               .setAlphaBlend()
               .setCullMode(rhi::CullMode::None)
               .setColorFormat(m_renderer->getDrawColorFormat())
               .setDepthFormat(m_renderer->getDrawDepthFormat());

        auto desc = builder.buildGraphics();

        desc.depthFormat = m_renderer->getDrawDepthFormat();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void InfiniteGrid::draw(rhi::RHICommandList* cmd, const Camera& camera) const
    {
        if (!m_pipeline || (m_renderer == nullptr))
        {
            return;
        }

        rhi::RHIPipeline* rhiPipe = m_renderer->getPipeline(m_pipeline);
        if (rhiPipe == nullptr)
        {
            return;
        }

        cmd->bindPipeline(m_renderer->getPipeline(m_pipeline));

        gpu::GridPushConstants pc{};
        pc.mvp = camera.proj() * camera.view();
        pc.cameraPos = glm::vec4(camera.position(), 1);
        pc.origin = glm::vec4(0);
        cmd->pushConstants(rhiPipe,
                           rhi::ShaderStage::Vertex,
                           0,
                           sizeof(pc),
                           &pc);

        cmd->draw(6, 1, 0, 0);
    }
}
