#include "pnkr/renderer/utils/FullScreenPass.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer::utils
{
    void FullScreenPass::init(RHIRenderer& renderer, const FullScreenPassConfig& config)
    {
        m_renderer = &renderer;

        // 1. Load Vertex Shader (Engine internal asset)
        // add_shader_target compiles this to the binary/shaders directory
        auto vert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/fullscreen.vert.spv");
        
        // 2. Load Fragment Shader (User provided)
        auto frag = rhi::Shader::load(rhi::ShaderStage::Fragment, config.fragmentShaderPath);

        if (!vert || !frag) {
            core::Logger::error("[FullScreenPass] Failed to load shaders for {}", config.debugName);
            return;
        }

        // 3. Build Pipeline
        rhi::RHIPipelineBuilder builder;
        builder.setShaders(vert.get(), frag.get())
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setCullMode(rhi::CullMode::None) // Fullscreen triangle doesn't need culling
               .setColorFormat(config.colorFormat)
               .setName(config.debugName);

        // Depth Configuration
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

        // Blend Configuration
        if (config.enableBlending) {
            builder.setAlphaBlend();
        } else {
            builder.setNoBlend();
        }

        // Note: We do NOT define a Vertex Input layout (useVertexType).
        // This relies on the vertex shader generating positions from gl_VertexIndex.

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void FullScreenPass::draw(rhi::RHICommandBuffer* cmd)
    {
        if (m_pipeline == INVALID_PIPELINE_HANDLE || !m_renderer) return;

        m_renderer->bindPipeline(cmd, m_pipeline);
        
        // Draw 3 vertices. The vertex shader generates the large triangle.
        cmd->draw(3, 1, 0, 0);
    }
}
