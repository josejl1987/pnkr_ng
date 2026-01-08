#include <cstddef>
#include <array>

#include "pnkr/renderer/passes/OITPass.hpp"
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
    void OITPass::init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                       ShaderHotReloader* hotReloader)
    {
        m_renderer = renderer;
        m_hotReloader = hotReloader;
        m_width = width;
        m_height = height;

        auto sVert = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/indirect.vert.spv");
        auto sFrag = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/indirect_oit.frag.spv");

        if (!sVert || !sFrag)
        {
            core::Logger::Render.error("OITPass: Failed to load indirect shaders.");
            return;
        }

        rhi::RHIPipelineBuilder oitBuilder;
        oitBuilder.setShaders(sVert.get(), sFrag.get())
            .setTopology(rhi::PrimitiveTopology::TriangleList)
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setColorFormats({})
            .setMultisampling(m_msaa.sampleCount, m_msaa.sampleShading, m_msaa.minSampleShading)
            .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
            .setNoBlend()
            .setName("OIT_Geometry");
        auto oitDesc = oitBuilder.buildGraphics();
        if (m_hotReloader != nullptr) {
            std::array sources = {
                ShaderSourceInfo{
                    .path =
                        "/shaders/renderer/indirect/indirect.slang",
                    .entryPoint = "vertexMain",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}},
                ShaderSourceInfo{
                    .path =
                        "/shaders/renderer/indirect/indirect.slang",
                    .entryPoint = "oitFragmentMain",
                    .stage = rhi::ShaderStage::Fragment,
                    .dependencies = {}}};
            m_oitPipeline = m_hotReloader->createGraphicsPipeline(oitDesc, sources);
        } else {
            m_oitPipeline = m_renderer->createGraphicsPipeline(oitDesc);
        }

        auto vComp = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/fullscreen_vert.spv");
        auto sFragComp = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/oit_composite.frag.spv");

        if (!vComp || !sFragComp)
        {
            core::Logger::Render.error("OITPass: Failed to load composite shaders.");
            return;
        }

        rhi::RHIPipelineBuilder compBuilder;
        compBuilder.setShaders(vComp.get(), sFragComp.get())
                   .setTopology(rhi::PrimitiveTopology::TriangleList)
                   .setCullMode(rhi::CullMode::None)
                   .enableDepthTest(false)
                   .setNoBlend()
                   .setColorFormat(rhi::Format::B10G11R11_UFLOAT_PACK32)
                   .setMultisampling(m_msaa.sampleCount, m_msaa.sampleShading, m_msaa.minSampleShading)
                   .setName("OIT_Composite");
        auto compDesc = compBuilder.buildGraphics();
        if (m_hotReloader != nullptr) {
            std::array sources = {
                ShaderSourceInfo{
                    .path =
                        "/shaders/renderer/post/PostProcess.slang",
                    .entryPoint = "fullscreenVert",
                    .stage = rhi::ShaderStage::Vertex,
                    .dependencies = {}},
                ShaderSourceInfo{
                    .path =
                        "/shaders/renderer/indirect/oit_composite.slang",
                    .entryPoint = "fragmentMain",
                    .stage = rhi::ShaderStage::Fragment,
                    .dependencies = {}}};
            m_compositePipeline = m_hotReloader->createGraphicsPipeline(compDesc, sources);
        } else {
            m_compositePipeline = m_renderer->createGraphicsPipeline(compDesc);
        }

        createResources(width, height);
    }

    void OITPass::resize(uint32_t width, uint32_t height,
                         const MSAASettings & msaa) {
      if (m_width == width && m_height == height && m_msaa.sampleCount == msaa.sampleCount && m_msaa.sampleShading == msaa.sampleShading) {
        return;
      }
      m_msaa = msaa;
      m_width = width;
      m_height = height;
      init(m_renderer, width, height, m_hotReloader);
    }

    void OITPass::createResources(uint32_t width, uint32_t height)
    {
        rhi::TextureDescriptor headDesc{};
        headDesc.extent = {.width = width, .height = height, .depth = 1};
        headDesc.format = rhi::Format::R32_UINT;
        headDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::TransferDst | rhi::TextureUsage::Sampled;
        headDesc.debugName = "OIT Heads";
        m_oitHeads = m_renderer->createTexture("OIT_Heads", headDesc);

        m_renderer->device()->immediateSubmit([&](rhi::RHICommandList* cmd) {
            rhi::RHIMemoryBarrier b{};
            b.texture = m_renderer->getTexture(m_oitHeads);
            b.srcAccessStage = rhi::ShaderStage::None;
            b.dstAccessStage = rhi::ShaderStage::Compute;
            b.oldLayout = rhi::ResourceLayout::Undefined;
            b.newLayout = rhi::ResourceLayout::General;
            cmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::Compute, b);
        });

        m_oitCounter = m_renderer->createBuffer("OIT_Counters", {
            .size = sizeof(uint32_t),
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::TransferDst | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .debugName = "OIT Counter"
        });

        const uint64_t nodeBufferSize =
            static_cast<const uint64_t>(8000000 * 16);
        m_oitNodes = m_renderer->createBuffer("OIT_Nodes", {
            .size = nodeBufferSize,
            .usage = rhi::BufferUsage::StorageBuffer | rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = rhi::MemoryUsage::GPUOnly,
            .debugName = "OIT Nodes"
        });

        rhi::TextureDescriptor copyDesc{};
        copyDesc.extent = {.width = width, .height = height, .depth = 1};
        copyDesc.format = rhi::Format::B10G11R11_UFLOAT_PACK32;
        copyDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        copyDesc.debugName = "SceneColorCopy";
        m_sceneColorCopy = m_renderer->createTexture("SceneColorCopy", copyDesc);
    }

    void OITPass::clear(rhi::RHICommandList* cmd)
    {

        cmd->fillBuffer(m_renderer->getBuffer(m_oitCounter), 0, sizeof(uint32_t), 0);

        rhi::ClearValue clearVal{};
        clearVal.color.uint32[0] = BINDLESS_INVALID_ID;
        clearVal.color.uint32[1] = BINDLESS_INVALID_ID;
        clearVal.color.uint32[2] = BINDLESS_INVALID_ID;
        clearVal.color.uint32[3] = BINDLESS_INVALID_ID;
        cmd->clearImage(m_renderer->getTexture(m_oitHeads), clearVal, rhi::ResourceLayout::General);
    }

    void OITPass::executeGeometry(const RenderPassContext& ctx, rhi::RHITexture* depthTexture)
    {
        const auto* dod = static_cast<const scene::GLTFUnifiedDODContext*>(ctx.resources.drawLists);
        uint32_t transparentCount =
            (dod != nullptr) ? dod->transparentCount : 0;
        auto* transparentBuf = m_renderer->getBuffer(ctx.frameBuffers.indirectTransparentBuffer.buffer.handle());
        if ((transparentBuf == nullptr) || transparentCount == 0) {
          return;
        }

        PNKR_PROFILE_SCOPE("Record OIT Geometry");
        using namespace passes::utils;

        ScopedGpuMarker geomScope(ctx.cmd, "OIT Geometry");
        RenderingInfoBuilder builder;
        builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
            .setDepthAttachment(
                depthTexture ? depthTexture : m_renderer->getTexture(ctx.resources.sceneDepth),
                rhi::LoadOp::Load, rhi::StoreOp::Store);

        ctx.cmd->beginRendering(builder.get());
        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);

        ctx.cmd->bindPipeline(m_renderer->getPipeline(m_oitPipeline));

        gpu::OITPushConstants pc = {};

        pc.indirect.cameraData = ctx.sceneDataAddr;
        pc.indirect.instances = ctx.instanceXformAddr;

        const bool hasSkinning = ctx.frameBuffers.jointMatricesBuffer.isValid();
        const bool hasMorphing = (ctx.model->morphVertexBuffer() != INVALID_BUFFER_HANDLE &&
            ctx.frameBuffers.morphStateBuffer.isValid());

        pc.indirect.vertices = (hasSkinning || hasMorphing)
                                          ? m_renderer->getBuffer(ctx.frameBuffers.skinnedVertexBuffer)->getDeviceAddress()
                                          : m_renderer->getBuffer(ctx.model->vertexBuffer())->getDeviceAddress();
        pc.indirect.materials = ctx.materialAddr;
        pc.indirect.lights = ctx.lightAddr;
        pc.indirect.shadowData = ctx.shadowDataAddr;
        pc.indirect.envMapData = ctx.environmentAddr;

        if (ctx.resources.transmissionTexture != INVALID_TEXTURE_HANDLE) {
            pc.transmissionFramebufferIndex = util::u32(m_renderer->getTextureBindlessIndex(ctx.resources.transmissionTexture));
            pc.transmissionFramebufferSamplerIndex = util::u32(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
        }

        pc.oitHeadsTextureIndex = util::u32(m_renderer->getStorageImageBindlessIndex(m_oitHeads));
        pc.oitNodeBufferPtr = m_renderer->getBuffer(m_oitNodes)->getDeviceAddress();
        pc.oitCounterPtr = m_renderer->getBuffer(m_oitCounter)->getDeviceAddress();
        pc.maxNodes = 8000000;

        ctx.cmd->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

        ctx.cmd->drawIndexedIndirect(transparentBuf, ctx.frameBuffers.indirectTransparentBuffer.offset + 16, transparentCount, sizeof(gpu::DrawIndexedIndirectCommandGPU));

        ctx.cmd->endRendering();
    }

    void OITPass::executeComposite(const RenderPassContext& ctx, rhi::RHITexture* targetTexture)
    {
        PNKR_PROFILE_SCOPE("Record OIT Composite");
        using namespace passes::utils;
        ScopedGpuMarker compScope(ctx.cmd, "OIT Composite");

        RenderingInfoBuilder builder;
        builder.setRenderArea(ctx.viewportWidth, ctx.viewportHeight)
            .addColorAttachment(
                targetTexture ? targetTexture : m_renderer->getTexture(ctx.resources.sceneColor),
                rhi::LoadOp::Load, rhi::StoreOp::Store);

        ctx.cmd->beginRendering(builder.get());
        setFullViewport(ctx.cmd, ctx.viewportWidth, ctx.viewportHeight);
        ctx.cmd->bindPipeline(m_renderer->getPipeline(m_compositePipeline));

        OITCompositePushConstants cpc{};
        cpc.nodeBufferPtr = m_renderer->getBuffer(m_oitNodes)->getDeviceAddress();
        cpc.headsTextureIndex = util::u32(m_renderer->getStorageImageBindlessIndex(m_oitHeads));
        cpc.backgroundTextureIndex = util::u32(m_renderer->getTextureBindlessIndex(m_sceneColorCopy));
        cpc.samplerIndex = util::u32(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));

        ctx.cmd->pushConstants(rhi::ShaderStage::Fragment, cpc);
        ctx.cmd->draw(3, 1, 0, 0);
        ctx.cmd->endRendering();
    }

    void OITPass::execute(const RenderPassContext& ctx)
    {
        clear(ctx.cmd);
        executeGeometry(ctx);
        executeComposite(ctx);
    }
}
