#include "pnkr/renderer/passes/WBOITPass.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/common.hpp"
#include "pnkr/renderer/gpu_shared/OITShared.h"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"
#include "pnkr/renderer/ShaderHotReloader.hpp"

namespace pnkr::renderer
{
    using namespace gpu;

    void WBOITPass::init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                         ShaderHotReloader* hotReloader)
    {
        m_renderer = renderer;
        m_hotReloader = hotReloader;
        m_width = width;
        m_height = height;

        auto sVertGeom = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/wboit_geometry.vert.spv");
        auto sFragGeom = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/wboit_geometry.frag.spv");

        if (!sVertGeom || !sFragGeom)
        {
            core::Logger::Render.error("WBOITPass: Failed to load geometry shaders.");
            return;
        }

        rhi::RHIPipelineBuilder oitBuilder;
        oitBuilder.setShaders(sVertGeom.get(), sFragGeom.get())
            .setTopology(rhi::PrimitiveTopology::TriangleList)
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setColorFormats(
                {rhi::Format::R16G16B16A16_SFLOAT, rhi::Format::R16_SFLOAT})
            .setMultisampling(m_msaa.sampleCount, false, 1.0f)
            .enableDepthTest(true, rhi::CompareOp::LessOrEqual, false)
            .setBlend(0, rhi::BlendOp::Add, rhi::BlendFactor::One,
                      rhi::BlendFactor::One)
            .setBlend(1, rhi::BlendOp::Add, rhi::BlendFactor::Zero,
                      rhi::BlendFactor::OneMinusSrcColor)
            .setName("WBOIT_Geometry");
        auto geomDesc = oitBuilder.buildGraphics();
        if (m_hotReloader != nullptr) {
            std::array sources = {
                ShaderSourceInfo{
                    .path =
                        "engine/src/renderer/shaders/renderer/indirect/"
                        "wboit_geometry.slang",
                    .entryPoint = "vertexMain",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}},
                ShaderSourceInfo{
                    .path =
                        "engine/src/renderer/shaders/renderer/indirect/"
                        "wboit_geometry.slang",
                    .entryPoint = "fragmentMain",
                    .stage = rhi::ShaderStage::Fragment,
                    .dependencies = {}}};
            m_geometryPipeline = m_hotReloader->createGraphicsPipeline(geomDesc, sources);
        } else {
            m_geometryPipeline = m_renderer->createGraphicsPipeline(geomDesc);
        }
        auto sVertComp = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/wboit_composite.vert.spv");
        auto sFragComp = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/wboit_composite.frag.spv");

        if (!sVertComp || !sFragComp)
        {
            core::Logger::Render.error("WBOITPass: Failed to load composite shaders.");
            return;
        }

        rhi::RHIPipelineBuilder compBuilder;
        compBuilder.setShaders(sVertComp.get(), sFragComp.get())
                   .setTopology(rhi::PrimitiveTopology::TriangleList)
                   .setCullMode(rhi::CullMode::None)
                   .enableDepthTest(false)
                   .setColorFormat(rhi::Format::B10G11R11_UFLOAT_PACK32)
                   .setMultisampling(1, false, 1.0f)
                   .setAlphaBlend()
                   .setName("WBOIT_Composite");
        auto compDesc = compBuilder.buildGraphics();
        if (m_hotReloader != nullptr) {
            std::array sources = {
                ShaderSourceInfo{
                    .path =
                        "engine/src/renderer/shaders/renderer/indirect/"
                        "wboit_composite.slang",
                    .entryPoint = "vertexMain",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}},
                ShaderSourceInfo{
                    .path =
                        "engine/src/renderer/shaders/renderer/indirect/"
                        "wboit_composite.slang",
                    .entryPoint = "fragmentMain",
                    .stage = rhi::ShaderStage::Fragment,
                    .dependencies = {}}};
            m_compositePipeline = m_hotReloader->createGraphicsPipeline(compDesc, sources);
        } else {
            m_compositePipeline = m_renderer->createGraphicsPipeline(compDesc);
        }
        createResources(width, height);
    }

    void WBOITPass::resize(uint32_t width, uint32_t height,
                           const MSAASettings & msaa) {
      if (m_width == width && m_height == height && m_msaa.sampleCount == msaa.sampleCount && m_msaa.sampleShading == msaa.sampleShading) {
        return;
      }
      m_msaa = msaa;
      m_width = width;
      m_height = height;
      init(m_renderer, width, height, m_hotReloader);
    }

    void WBOITPass::createResources(uint32_t width, uint32_t height)
    {
        using namespace passes::utils;

        const uint32_t samples = m_msaa.sampleCount;

        createTextureAttachment(m_renderer, m_accumTexture, width, height,
            rhi::Format::R16G16B16A16_SFLOAT,
            rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc,
            "WBOIT Accum", samples);
        createTextureAttachment(m_renderer, m_revealTexture, width, height,
            rhi::Format::R16_SFLOAT,
            rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc,
            "WBOIT Reveal", samples);
        createTextureAttachment(m_renderer, m_sceneColorCopy, width, height,
            rhi::Format::B10G11R11_UFLOAT_PACK32,
            rhi::TextureUsage::TransferDst | rhi::TextureUsage::Sampled,
            "WBOIT SceneColorCopy"); // No samples needed here as it's a copy

        if (samples > 1) {
            createTextureAttachment(m_renderer, m_accumResolved, width, height,
                rhi::Format::R16G16B16A16_SFLOAT,
                rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                "WBOIT Accum Resolved", 1);
            createTextureAttachment(m_renderer, m_revealResolved, width, height,
                rhi::Format::R16_SFLOAT,
                rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
                "WBOIT Reveal Resolved", 1);
        } else {
            m_accumResolved = {};
            m_revealResolved = {};
        }
    }

    void WBOITPass::clear(rhi::RHICommandList* cmd)
    {
        rhi::ClearValue clearAccum{};
        clearAccum.color.float32[0] = 0.0F;
        clearAccum.color.float32[1] = 0.0F;
        clearAccum.color.float32[2] = 0.0F;
        clearAccum.color.float32[3] = 0.0F;

        rhi::ClearValue clearReveal{};
        clearReveal.color.float32[0] = 1.0F;
        clearReveal.color.float32[1] = 1.0F;
        clearReveal.color.float32[2] = 1.0F;
        clearReveal.color.float32[3] = 1.0F;

        cmd->clearImage(m_renderer->getTexture(m_accumTexture), clearAccum, rhi::ResourceLayout::General);
        cmd->clearImage(m_renderer->getTexture(m_revealTexture), clearReveal, rhi::ResourceLayout::General);
    }

    void WBOITPass::executeGeometry(const RenderPassContext& ctx, rhi::RHITexture* depthTexture)
    {
        const auto* dod = static_cast<const scene::GLTFUnifiedDODContext*>(ctx.resources.drawLists);
        uint32_t transparentCount =
            (dod != nullptr) ? dod->transparentCount : 0;
        auto* transparentBuf = m_renderer->getBuffer(ctx.frameBuffers.indirectTransparentBuffer.buffer.handle());

        core::Logger::Render.trace("WBOITPass: dod={} transparentCount={} bufHandle={} buf={}",
            (void*)dod, transparentCount,
            static_cast<uint32_t>(ctx.frameBuffers.indirectTransparentBuffer.buffer),
            (void*)transparentBuf);

        if ((transparentBuf == nullptr) || transparentCount == 0) {
          return;
        }

        PNKR_PROFILE_SCOPE("Record WBOIT Geometry");
        using namespace passes::utils;
        ScopedGpuMarker geomScope(ctx.cmd, "WBOIT Geometry");

        rhi::ClearValue clearAccum{};
        clearAccum.color.float32[0] = 0.0F;
        clearAccum.color.float32[1] = 0.0F;
        clearAccum.color.float32[2] = 0.0F;
        clearAccum.color.float32[3] = 0.0F;

        rhi::ClearValue clearReveal{};
        clearReveal.color.float32[0] = 1.0F;
        clearReveal.color.float32[1] = 1.0F;
        clearReveal.color.float32[2] = 1.0F;
        clearReveal.color.float32[3] = 1.0F;

        rhi::RenderingInfo renderInfo{};
        renderInfo.renderArea = {.x = 0,
                                 .y = 0,
                                 .width = ctx.viewportWidth,
                                 .height = ctx.viewportHeight};

        rhi::RenderingAttachment depthAttachment{};

        depthAttachment.texture = depthTexture ? depthTexture : m_renderer->getTexture(ctx.resources.sceneDepth);
        depthAttachment.loadOp = rhi::LoadOp::Load;
        depthAttachment.storeOp = rhi::StoreOp::Store;
        renderInfo.depthAttachment = &depthAttachment;

        rhi::RenderingAttachment accumAttachment{};
        accumAttachment.texture = m_renderer->getTexture(m_accumTexture);
        accumAttachment.loadOp = rhi::LoadOp::Clear;
        accumAttachment.storeOp = rhi::StoreOp::Store;
        accumAttachment.clearValue = clearAccum;

        rhi::RenderingAttachment revealAttachment{};
        revealAttachment.texture = m_renderer->getTexture(m_revealTexture);
        revealAttachment.loadOp = rhi::LoadOp::Clear;
        revealAttachment.storeOp = rhi::StoreOp::Store;
        revealAttachment.clearValue = clearReveal;

        renderInfo.colorAttachments = {accumAttachment, revealAttachment};

        ctx.cmd->beginRendering(renderInfo);
        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);

        ctx.cmd->bindPipeline(m_renderer->getPipeline(m_geometryPipeline));

        gpu::WBOITPushConstants pc = {};
        populateBaseIndirectPushConstants(ctx, pc.indirect, m_renderer);

        pc.accumTextureIndex = util::u32(m_renderer->getTextureBindlessIndex(m_accumTexture));
        pc.revealTextureIndex = util::u32(m_renderer->getTextureBindlessIndex(m_revealTexture));

        ctx.cmd->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

        ctx.cmd->drawIndexedIndirect(transparentBuf, ctx.frameBuffers.indirectTransparentBuffer.offset + 16, transparentCount, sizeof(gpu::DrawIndexedIndirectCommandGPU));

        ctx.cmd->endRendering();
    }

    void WBOITPass::executeComposite(const RenderPassContext& ctx, rhi::RHITexture* targetTexture)
    {
        PNKR_PROFILE_SCOPE("Record WBOIT Composite");
        using namespace passes::utils;
        ScopedGpuMarker compScope(ctx.cmd, "WBOIT Composite");

        rhi::RHITexture* accumTex = m_renderer->getTexture(m_accumTexture);
        rhi::RHITexture* revealTex = m_renderer->getTexture(m_revealTexture);

        if (accumTex->sampleCount() > 1 && m_accumResolved.isValid() && m_revealResolved.isValid()) {
            rhi::TextureCopyRegion region{};
            region.srcSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.dstSubresource = {.mipLevel = 0, .arrayLayer = 0};
            region.extent = {.width = ctx.viewportWidth,
                             .height = ctx.viewportHeight,
                             .depth = 1};
            region.srcOffsets[0] = {0, 0, 0};
            region.dstOffsets[0] = {0, 0, 0};

            ctx.cmd->resolveTexture(accumTex, rhi::ResourceLayout::General,
                                    m_renderer->getTexture(m_accumResolved), rhi::ResourceLayout::General, region);
            ctx.cmd->resolveTexture(revealTex, rhi::ResourceLayout::General,
                                    m_renderer->getTexture(m_revealResolved), rhi::ResourceLayout::General, region);
            
            accumTex = m_renderer->getTexture(m_accumResolved);
            revealTex = m_renderer->getTexture(m_revealResolved);
        }

        RenderingInfoBuilder builder;
        builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
            .addColorAttachment(
                targetTexture ? targetTexture : m_renderer->getTexture(ctx.resources.sceneColor),
                rhi::LoadOp::Load, rhi::StoreOp::Store);

        ctx.cmd->beginRendering(builder.get());
        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);
        ctx.cmd->bindPipeline(m_renderer->getPipeline(m_compositePipeline));

        WBOITCompositePushConstants cpc{};
        cpc.accumTextureIndex = util::u32(accumTex->getBindlessHandle().index());
        cpc.revealTextureIndex = util::u32(revealTex->getBindlessHandle().index());
        cpc.backgroundTextureIndex = util::u32(ctx.fg->getTexture(ctx.fgSceneColorCopy)->getBindlessHandle());
        cpc.samplerIndex = util::u32(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
        cpc.opacityBoost = 0.0F;
        cpc.showHeatmap = 0;

        ctx.cmd->pushConstants(rhi::ShaderStage::Fragment, cpc);
        ctx.cmd->draw(3, 1, 0, 0);
        ctx.cmd->endRendering();
    }

    void WBOITPass::execute(const RenderPassContext& ctx)
    {
        clear(ctx.cmd);
        executeGeometry(ctx);
        executeComposite(ctx);
    }
}
