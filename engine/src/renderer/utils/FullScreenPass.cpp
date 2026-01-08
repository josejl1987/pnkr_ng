#include "pnkr/renderer/utils/FullScreenPass.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::utils
{
    void FullScreenPass::init(RHIRenderer& renderer, const FullScreenPassConfig& config)
    {
        m_renderer = &renderer;

        auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/fullscreen_vert.spv");

        auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, config.fragmentShaderPath);

        if (!vert || !frag) {
            core::Logger::Render.error("[FullScreenPass] Failed to load shaders for {}", config.debugName);
            return;
        }

        rhi::RHIPipelineBuilder builder;
        builder.setShaders(vert.get(), frag.get())
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setCullMode(rhi::CullMode::None)
               .setColorFormat(config.colorFormat)
               .setName(config.debugName);

        if (config.depthFormat != rhi::Format::Undefined) {
            builder.setDepthFormat(config.depthFormat);
            if (config.enableDepthTest) {
                builder.enableDepthTest(config.enableDepthWrite, rhi::CompareOp::LessOrEqual);
            } else {
                builder.disableDepthTest();
            }
        } else {
            builder.disableDepthTest();
        }

        if (config.enableBlending) {
            builder.setAlphaBlend();
        } else {
            builder.setNoBlend();
        }

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void FullScreenPass::draw(rhi::RHICommandList *cmd) const {
      if (m_pipeline == INVALID_PIPELINE_HANDLE || (m_renderer == nullptr)) {
        return;
      }

      cmd->bindPipeline(m_renderer->getPipeline(m_pipeline));

      cmd->draw(3, 1, 0, 0);
    }
}
