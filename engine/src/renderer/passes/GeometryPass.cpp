#include "pnkr/renderer/passes/GeometryPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/gpu_shared/SkyboxShared.h"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"

namespace pnkr::renderer
{
    using namespace gpu;
    void GeometryPass::init(RHIRenderer *renderer, uint32_t ,
                            uint32_t ) {
      m_renderer = renderer;

      auto sVert = rhi::Shader::load(rhi::ShaderStage::Vertex,
                                     "shaders/indirect.vert.spv");
      auto sFrag = rhi::Shader::load(rhi::ShaderStage::Fragment,
                                     "shaders/indirect.frag.spv");

      if (!sVert || !sFrag) {
        core::Logger::Render.error("GeometryPass: Failed to load shaders.");
        return;
      }

      rhi::RHIPipelineBuilder builder;
      builder.setShaders(sVert.get(), sFrag.get())
          .setTopology(rhi::PrimitiveTopology::TriangleList)
          .setPolygonMode(rhi::PolygonMode::Fill)
          .setCullMode(rhi::CullMode::Back, true, false)
          .setMultisampling(m_msaa.sampleCount, m_msaa.sampleShading,
                            m_msaa.minSampleShading)
          .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
          .setNoBlend()
          .setDepthFormat(m_renderer->getDrawDepthFormat())
          .setColorFormat(rhi::Format::B10G11R11_UFLOAT_PACK32)
          .setName("IndirectSolid");

      m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());

      builder.setCullMode(rhi::CullMode::None, true, false)
          .setName("IndirectSolidDoubleSided");
      m_pipelineDoubleSided =
          m_renderer->createGraphicsPipeline(builder.buildGraphics());

      builder.setCullMode(rhi::CullMode::Back, true, false)
          .setAlphaBlend()
          .enableDepthTest(true, rhi::CompareOp::LessOrEqual, false)
          .setName("IndirectTransparent");
      m_pipelineTransparent =
          m_renderer->createGraphicsPipeline(builder.buildGraphics());

      builder.setCullMode(rhi::CullMode::Back, true, false)
          .setBlend(0, rhi::BlendOp::Add, rhi::BlendFactor::SrcAlpha,
                    rhi::BlendFactor::One, rhi::BlendOp::Add,
                    rhi::BlendFactor::One, rhi::BlendFactor::OneMinusSrcAlpha)
          .enableDepthTest(true, rhi::CompareOp::LessOrEqual, false)
          .setName("IndirectTransmission");
      m_pipelineTransmission =
          m_renderer->createGraphicsPipeline(builder.buildGraphics());

      builder.setCullMode(rhi::CullMode::None, true, false)
          .setName("IndirectTransmissionDoubleSided");
      m_pipelineTransmissionDoubleSided =
          m_renderer->createGraphicsPipeline(builder.buildGraphics());

      builder.setPolygonMode(rhi::PolygonMode::Line)
          .setName("IndirectWireframe");
      m_pipelineWireframe =
          m_renderer->createGraphicsPipeline(builder.buildGraphics());

      // Skybox Pipeline
      auto sSkyboxVert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/skybox.vert.spv");
      auto sSkyboxFrag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/skybox.frag.spv");

      if (sSkyboxVert && sSkyboxFrag) {
          builder.setShaders(sSkyboxVert.get(), sSkyboxFrag.get())
              .setTopology(rhi::PrimitiveTopology::TriangleList)
              .setPolygonMode(rhi::PolygonMode::Fill)
              .setCullMode(rhi::CullMode::None, false) // Inside-out cube or ignore culling for skybox
              .setMultisampling(m_msaa.sampleCount, m_msaa.sampleShading, m_msaa.minSampleShading)
              .enableDepthTest(true, rhi::CompareOp::LessOrEqual, false) // Read-only depth
              .setNoBlend()
              .setColorFormat(rhi::Format::B10G11R11_UFLOAT_PACK32)
              .setName("IndirectSkybox");
          
          m_pipelineSkybox = m_renderer->createGraphicsPipeline(builder.buildGraphics());
      } else {
          core::Logger::Render.error("GeometryPass: Failed to load skybox shaders.");
      }
    }

    void GeometryPass::resize(uint32_t width, uint32_t height, const MSAASettings& msaa)
    {
        if (m_msaa.sampleCount != msaa.sampleCount || m_msaa.sampleShading != msaa.sampleShading)
        {
            m_msaa = msaa;
            init(m_renderer, width, height);
        }
    }

    void GeometryPass::execute(const RenderPassContext& )
    {
        core::Logger::Render.error("GeometryPass::execute called without render targets! Use executeMain instead.");
    }

    void GeometryPass::executeMain(const RenderPassContext& ctx, rhi::RHITexture* color, rhi::RHITexture* depth, rhi::RHITexture* resolveColor, rhi::RHITexture* resolveDepth)
    {
        using namespace passes::utils;
        ScopedGpuMarker scope(ctx.cmd, "Geometry Pass");

        RenderingInfoBuilder builder;
        builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight);
        
        if (resolveColor && resolveColor != color) {
            builder.addColorAttachment(color, rhi::LoadOp::Clear, rhi::StoreOp::Store, resolveColor);
        } else {
             builder.addColorAttachment(color, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        }

        if (resolveDepth && resolveDepth != depth) {
             builder.setDepthAttachment(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store, resolveDepth);
        } else {
             builder.setDepthAttachment(depth, rhi::LoadOp::Clear, rhi::StoreOp::Store);
        }

        ctx.cmd->beginRendering(builder.get());
        
        gpu::OITPushConstants pc{};

        pc.indirect.cameraData = ctx.cameraDataAddr;
        pc.indirect.instances = ctx.instanceXformAddr;

        const bool hasSkinning = ctx.frameBuffers.jointMatricesBuffer.isValid();
        const bool hasMorphing = (ctx.model->morphVertexBuffer() != INVALID_BUFFER_HANDLE && ctx.frameBuffers.morphStateBuffer.isValid());

        pc.indirect.vertices = (hasSkinning || hasMorphing)
                                ? m_renderer->getBuffer(ctx.frameBuffers.skinnedVertexBuffer)->getDeviceAddress()
                                : m_renderer->getBuffer(ctx.model->vertexBuffer())->getDeviceAddress();

        pc.indirect.materials = ctx.materialAddr;
        pc.indirect.lights = ctx.lightAddr;
        pc.indirect.lightCount = ctx.lightCount;
        pc.indirect.shadowData = ctx.shadowDataAddr;
        pc.indirect.envMapData = ctx.environmentAddr;

        m_renderer->device()->auditBDA(pc.indirect.cameraData, "GeometryPass::cameraData");
        m_renderer->device()->auditBDA(pc.indirect.instances, "GeometryPass::instances");
        m_renderer->device()->auditBDA(pc.indirect.vertices, "GeometryPass::vertices");
        m_renderer->device()->auditBDA(pc.indirect.materials, "GeometryPass::materials");
        m_renderer->device()->auditBDA(pc.indirect.lights, "GeometryPass::lights");
        m_renderer->device()->auditBDA(pc.indirect.shadowData, "GeometryPass::shadowData");
        m_renderer->device()->auditBDA(pc.indirect.envMapData, "GeometryPass::envMapData");

        if (ctx.model->indexBuffer() == INVALID_BUFFER_HANDLE)
        {
          static bool sWarned = false;
          if (!sWarned) {
            core::Logger::Render.warn(
                "GeometryPass: Missing index buffer for model. Skipping draw.");
            sWarned = true;
          }
           ctx.cmd->endRendering();
            return;
        }

        ctx.cmd->bindIndexBuffer(m_renderer->getBuffer(ctx.model->indexBuffer()), 0, false);

        drawOpaque(ctx, pc);
        
        ctx.cmd->endRendering();
    }

    void GeometryPass::drawOpaque(const RenderPassContext& ctx, const OITPushConstants& pc)
    {
        PNKR_PROFILE_SCOPE("Record Opaque Pass");
        using namespace passes::utils;

        ScopedPassMarkers passScope(ctx.cmd, "Opaque Pass", 0.2F, 0.4F, 0.8F,
                                    1.0F);

        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);

        PipelineHandle opaquePipeline = ctx.settings.drawWireframe ? m_pipelineWireframe : m_pipeline;
        PipelineHandle doubleSidedPipeline = ctx.settings.drawWireframe ? m_pipelineWireframe : m_pipelineDoubleSided;

        if (ctx.model->indexBuffer() != INVALID_BUFFER_HANDLE)
        {
            ctx.cmd->bindIndexBuffer(m_renderer->getBuffer(ctx.model->indexBuffer()), 0, false);
        }

        auto* outBuf = m_renderer->getBuffer(ctx.frameBuffers.opaqueCompactedSlice.buffer);
        if (outBuf != nullptr) {
          ctx.cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline));
          ctx.cmd->pushConstants(
              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

          const uint32_t maxDrawCount =
              (ctx.resources.drawLists != nullptr)
                  ? ctx.resources.drawLists->opaqueBoundsCount
                  : 0U;

          const auto &s = ctx.frameBuffers.opaqueCompactedSlice;
          const auto commandsOffset = uint32_t(s.offset + s.dataOffset);
          const auto countOffset = uint32_t(s.offset + 0);

          ctx.cmd->drawIndexedIndirectCount(
              outBuf, commandsOffset, outBuf, countOffset, maxDrawCount,
              sizeof(DrawIndexedIndirectCommandGPU));
        } else {
          auto *fallbackBuf = m_renderer->getBuffer(
              ctx.frameBuffers.indirectOpaqueBuffer.buffer);
          const auto &s = ctx.frameBuffers.indirectOpaqueBuffer;
          const auto commandsOffset = uint32_t(s.offset + 16);
          const auto *dodLists =
              static_cast<const scene::GLTFUnifiedDODContext *>(
                  ctx.resources.drawLists);
          const uint32_t drawCount =
              (dodLists != nullptr) ? dodLists->opaqueCount : 0;

          if ((fallbackBuf != nullptr) && drawCount > 0) {
            ctx.cmd->bindPipeline(m_renderer->getPipeline(opaquePipeline));
            ctx.cmd->pushConstants(
                rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);
            ctx.cmd->drawIndexedIndirect(fallbackBuf, commandsOffset, drawCount,
                                         sizeof(DrawIndexedIndirectCommandGPU));
          }
        }

        auto* dsOutBuf = m_renderer->getBuffer(ctx.frameBuffers.opaqueDoubleSidedCompactedSlice.buffer);
        if (dsOutBuf != nullptr) {
          ctx.cmd->bindPipeline(m_renderer->getPipeline(doubleSidedPipeline));
          ctx.cmd->pushConstants(
              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

          const uint32_t maxDrawCountDS =
              (ctx.resources.drawLists != nullptr)
                  ? ctx.resources.drawLists->opaqueDoubleSidedBoundsCount
                  : 0U;

          const auto &s = ctx.frameBuffers.opaqueDoubleSidedCompactedSlice;
          const auto commandsOffset = uint32_t(s.offset + s.dataOffset);
          const auto countOffset = uint32_t(s.offset + 0);

          ctx.cmd->drawIndexedIndirectCount(
              dsOutBuf, commandsOffset, dsOutBuf, countOffset, maxDrawCountDS,
              sizeof(DrawIndexedIndirectCommandGPU));
        } else {
          auto *dsBuf = m_renderer->getBuffer(
              ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer.buffer);
          const auto &sDS = ctx.frameBuffers.indirectOpaqueDoubleSidedBuffer;
          const auto dsCommandsOffset = uint32_t(sDS.offset + 16);
          const auto *dodLists =
              static_cast<const scene::GLTFUnifiedDODContext *>(
                  ctx.resources.drawLists);
          const uint32_t dsDrawCount =
              (dodLists != nullptr) ? dodLists->opaqueDoubleSidedCount : 0;

          if ((dsBuf != nullptr) && dsDrawCount > 0) {
            ctx.cmd->bindPipeline(m_renderer->getPipeline(doubleSidedPipeline));
            ctx.cmd->pushConstants(
                rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);
            ctx.cmd->drawIndexedIndirect(dsBuf, dsCommandsOffset, dsDrawCount,
                                         sizeof(DrawIndexedIndirectCommandGPU));
          }
        }
    }

    void GeometryPass::drawTransparent(const RenderPassContext& ctx, const gpu::OITPushConstants& pc, bool drawTransmission, bool drawTransparentObjects)
    {
        PNKR_PROFILE_SCOPE("Record Transparent Pass");
        using namespace passes::utils;

        ScopedPassMarkers passScope(ctx.cmd, "Transparent Pass", 0.2F, 0.8F,
                                    0.4F, 1.0F);

        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);

        PipelineHandle transmissionPipeline = ctx.settings.drawWireframe ? m_pipelineWireframe : m_pipelineTransmission;
        PipelineHandle transmissionDSPipeline = ctx.settings.drawWireframe ? m_pipelineWireframe : m_pipelineTransmissionDoubleSided;

        ctx.cmd->bindIndexBuffer(m_renderer->getBuffer(ctx.model->indexBuffer()), 0, false);

        const auto* dod = static_cast<const scene::GLTFUnifiedDODContext*>(ctx.resources.drawLists);
        uint32_t transCount = (dod != nullptr) ? dod->transmissionCount : 0;
        uint32_t transDSCount =
            (dod != nullptr) ? dod->transmissionDoubleSidedCount : 0;
        uint32_t transparentCount =
            (dod != nullptr) ? dod->transparentCount : 0;

        auto pcCopy = pc;
        if (ctx.resources.transmissionTexture != INVALID_TEXTURE_HANDLE)
        {
            pcCopy.indirect.transmissionTexIndex = static_cast<uint32_t>(m_renderer->getTextureBindlessIndex(ctx.resources.transmissionTexture));
            pcCopy.indirect.transmissionSamplerIndex = static_cast<uint32_t>(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
        }

        auto* transBuf = m_renderer->getBuffer(ctx.frameBuffers.indirectTransmissionBuffer.buffer);
        if (drawTransmission && (transBuf != nullptr) && transCount > 0) {
          ctx.cmd->bindPipeline(m_renderer->getPipeline(transmissionPipeline));
          ctx.cmd->pushConstants(
              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pcCopy);
          ctx.cmd->drawIndexedIndirect(
              transBuf, ctx.frameBuffers.indirectTransmissionBuffer.offset + 16,
              transCount, sizeof(DrawIndexedIndirectCommandGPU));
        }

        auto* transDSBuf = m_renderer->getBuffer(ctx.frameBuffers.indirectTransmissionDoubleSidedBuffer.buffer);
        if (drawTransmission && (transDSBuf != nullptr) && transDSCount > 0) {
          ctx.cmd->bindPipeline(
              m_renderer->getPipeline(transmissionDSPipeline));
          ctx.cmd->pushConstants(
              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pcCopy);
          ctx.cmd->drawIndexedIndirect(
              transDSBuf,
              ctx.frameBuffers.indirectTransmissionDoubleSidedBuffer.offset +
                  16,
              transDSCount, sizeof(DrawIndexedIndirectCommandGPU));
        }

        auto* transparentBuf = m_renderer->getBuffer(ctx.frameBuffers.indirectTransparentBuffer.buffer);
        if (drawTransparentObjects && (transparentBuf != nullptr) &&
            transparentCount > 0) {
          ctx.cmd->bindPipeline(m_renderer->getPipeline(m_pipelineTransparent));
          ctx.cmd->pushConstants(
              rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pcCopy);
          ctx.cmd->drawIndexedIndirect(
              transparentBuf,
              ctx.frameBuffers.indirectTransparentBuffer.offset + 16,
              transparentCount, sizeof(DrawIndexedIndirectCommandGPU));
        }
    }
    
    void GeometryPass::drawSkybox(const RenderPassContext& ctx, rhi::RHITexture* color, rhi::RHITexture* depth) {
        if (!ctx.settings.enableSkybox || !m_pipelineSkybox.isValid() || ctx.resources.skyboxCubemap == INVALID_TEXTURE_HANDLE) return;
        
        using namespace passes::utils;
        
        RenderingInfoBuilder builder;
        builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
            .addColorAttachment(color, rhi::LoadOp::Load, rhi::StoreOp::Store) // Load existing color
            .setDepthAttachment(depth, rhi::LoadOp::Load, rhi::StoreOp::Store); // Load existing depth

        ctx.cmd->beginRendering(builder.get());
        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);

        ctx.cmd->bindPipeline(m_renderer->getPipeline(m_pipelineSkybox));
        
        gpu::SkyboxPushConstants pc{};
        pc.invViewProj = glm::inverse(ctx.camera->proj() * glm::mat4(glm::mat3(ctx.camera->view())));
        pc.textureIndex = static_cast<uint32_t>(m_renderer->getTextureBindlessIndex(ctx.resources.skyboxCubemap));
        pc.samplerIndex = static_cast<uint32_t>(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
        // Note: flipY and rotation could be passed if needed, currently assuming defaults or from settings
        // But ctx.settings.skyboxRotation is available. 
        pc.rotation = glm::radians(ctx.settings.skyboxRotation);
        pc.flipY = 0; // Usually 0 for engine-managed skyboxes, or check IndirectRenderer flipY state if exposed.
        
        ctx.cmd->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);
        
        // Draw empty triangle (vertices generated in shader)
        ctx.cmd->draw(3, 1, 0, 0);

        ctx.cmd->endRendering();
    }
}
