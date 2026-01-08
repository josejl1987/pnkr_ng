#include "pnkr/renderer/passes/SSAOPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/gpu_shared/PostProcessShared.h"
#include "pnkr/core/common.hpp"
#include "pnkr/renderer/ShaderHotReloader.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <random>
#include <array>

namespace pnkr::renderer
{
    void SSAOPass::init(RHIRenderer* renderer, uint32_t width, uint32_t height,
                        ShaderHotReloader* hotReloader)
    {
        m_renderer = renderer;
        m_hotReloader = hotReloader;
        m_width = width;
        m_height = height;

        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
        std::default_random_engine generator;
        std::array<glm::vec4, 16> ssaoNoise;
        for (uint32_t i = 0; i < 16; i++) {
          glm::vec3 noise((randomFloats(generator) * 2.0) - 1.0,
                          (randomFloats(generator) * 2.0) - 1.0,
                          (randomFloats(generator) * 2.0) - 1.0);
          noise = glm::normalize(noise);
          ssaoNoise[i] = glm::vec4(noise, 0.0F);
        }

        rhi::TextureDescriptor noiseDesc{};
        noiseDesc.extent = {.width = 4, .height = 4, .depth = 1};
        noiseDesc.format = rhi::Format::R32G32B32A32_SFLOAT;
        noiseDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        noiseDesc.debugName = "SSAORotation";
        m_rotationTexture = m_renderer->createTexture("SSAORotation", noiseDesc);
        m_renderer->getTexture(m_rotationTexture.handle())->uploadData(std::span<const std::byte>(reinterpret_cast<const std::byte*>(ssaoNoise.data()), ssaoNoise.size() * sizeof(glm::vec4)));

        rhi::RHIPipelineBuilder builder;
        auto sResolve = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/ssao_depth_resolve.spv");
        auto depthResolveDesc =
            builder.setComputeShader(sResolve.get()).setName("DepthResolve").buildCompute();
        if (m_hotReloader != nullptr) {
            ShaderSourceInfo source{
                .path = "/shaders/renderer/ssao/SSAO.slang",
                .entryPoint = "depthResolveMain",
                .stage = rhi::ShaderStage::Compute,
                .dependencies = {}};
            m_depthResolvePipeline = m_hotReloader->createComputePipeline(depthResolveDesc, source);
        } else {
            m_depthResolvePipeline = m_renderer->createComputePipeline(depthResolveDesc);
        }

        auto sGen = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/ssao.spv");
        auto ssaoDesc =
            builder.setComputeShader(sGen.get()).setName("SSAOGen").buildCompute();
        if (m_hotReloader != nullptr) {
            ShaderSourceInfo source{
                .path = "/shaders/renderer/ssao/SSAO.slang",
                .entryPoint = "ssaoMain",
                .stage = rhi::ShaderStage::Compute,
                .dependencies = {}};
            m_ssaoPipeline = m_hotReloader->createComputePipeline(ssaoDesc, source);
        } else {
            m_ssaoPipeline = m_renderer->createComputePipeline(ssaoDesc);
        }

        auto sBlur = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/ssao_blur.spv");
        auto blurDesc =
            builder.setComputeShader(sBlur.get()).setName("SSAOBlur").buildCompute();
        if (m_hotReloader != nullptr) {
            ShaderSourceInfo source{
                .path = "/shaders/renderer/ssao/SSAO.slang",
                .entryPoint = "blurMain",
                .stage = rhi::ShaderStage::Compute,
                .dependencies = {}};
            m_blurPipeline = m_hotReloader->createComputePipeline(blurDesc, source);
        } else {
            m_blurPipeline = m_renderer->createComputePipeline(blurDesc);
        }

        createResources(width, height);
    }

    void SSAOPass::createResources(uint32_t width, uint32_t height)
    {
        m_width = width;
        m_height = height;

        rhi::TextureDescriptor desc{};
        desc.extent = {.width = width, .height = height, .depth = 1};
        desc.format = rhi::Format::R16_SFLOAT;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        m_ssaoRaw = m_renderer->createTexture("SSAO_Raw", desc);
        m_ssaoIntermediate = m_renderer->createTexture("SSAO_Intermediate", desc);
        m_ssaoBlur = m_renderer->createTexture("SSAO_Blur", desc);

        rhi::TextureDescriptor depthDesc = desc;
        depthDesc.format = rhi::Format::R32_SFLOAT;
        m_depthResolved = m_renderer->createTexture("SSAO_DepthResolved", depthDesc);
    }

    void SSAOPass::executeGen(const RenderPassContext& ctx, rhi::RHICommandList* cmd)
    {
        using namespace passes::utils;
        ScopedPassMarkers genScope(cmd, "SSAO Gen", 0.9F, 0.7F, 0.3F, 1.0F);
        cmd->bindPipeline(m_renderer->getPipeline(m_ssaoPipeline.handle()));

        PNKR_ASSERT(ctx.fg, "SSAO pass requires FrameGraphResources");
        rhi::TextureBindlessHandle depthResolvedIdx = ctx.fg->getTextureIndex(ctx.fgDepthResolved);
        rhi::TextureBindlessHandle ssaoRawIdx = ctx.fg->getStorageImageIndex(ctx.fgSSAORaw);
        rhi::SamplerBindlessHandle repeatSampler = m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::Repeat);

        cmd->bindPipeline(m_renderer->getPipeline(m_ssaoPipeline.handle()));

        gpu::SSAOParams spc{};
        spc.depthTexID = util::u32(depthResolvedIdx);
        spc.rotationTexID = util::u32(m_renderer->getTextureBindlessIndex(m_rotationTexture.handle()));
        spc.outputTexID = util::u32(ssaoRawIdx);
        spc.samplerID = util::u32(repeatSampler);

        spc.zNear = ctx.camera->zNear();
        spc.zFar = ctx.camera->zFar();
        spc.radius = ctx.settings.ssao.radius;
        spc.attScale = ctx.settings.ssao.attScale;
        spc.distScale = ctx.settings.ssao.distScale;

        cmd->pushConstants(rhi::ShaderStage::Compute, spc);
        cmd->dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);
    }

    void SSAOPass::executeBlur(const RenderPassContext& ctx, rhi::RHICommandList* cmd)
    {
        using namespace passes::utils;
        ScopedPassMarkers blurScope(cmd, "SSAO Blur", 0.7F, 0.5F, 0.3F, 1.0F);

        PNKR_ASSERT(ctx.fg, "SSAO pass requires FrameGraphResources");
        rhi::TextureBindlessHandle depthResolvedIdx = ctx.fg->getTextureIndex(ctx.fgDepthResolved);
        rhi::TextureBindlessHandle ssaoRawIdx = ctx.fg->getTextureIndex(ctx.fgSSAORaw);
        rhi::TextureBindlessHandle ssaoBlurIdx = ctx.fg->getStorageImageIndex(ctx.fgSSAOBlur);
        rhi::SamplerBindlessHandle clampSampler = m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge);
        uint32_t ssaoIntermediateStorageIdx = util::u32(m_renderer->getStorageImageBindlessIndex(m_ssaoIntermediate));
        uint32_t ssaoIntermediateSampledIdx = util::u32(m_renderer->getTextureBindlessIndex(m_ssaoIntermediate));

        gpu::BlurParams bpc{};
        bpc.depthTexID = util::u32(depthResolvedIdx);
        bpc.samplerID = util::u32(clampSampler);
        bpc.sharpness = ctx.settings.ssao.blurSharpness;
        bpc.zNear = ctx.camera->zNear();
        bpc.zFar = ctx.camera->zFar();

        cmd->bindPipeline(m_renderer->getPipeline(m_blurPipeline.handle()));

        {
            rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_renderer->getTexture(m_ssaoIntermediate);
            barrier.srcAccessStage = rhi::ShaderStage::Compute;
            barrier.dstAccessStage = rhi::ShaderStage::Compute;
            barrier.oldLayout = rhi::ResourceLayout::Undefined;
            barrier.newLayout = rhi::ResourceLayout::General;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Compute, barrier);

            bpc.inputTexID = util::u32(ssaoRawIdx);
            bpc.outputTexID = ssaoIntermediateStorageIdx;
            bpc.axis = 0;
            cmd->pushConstants(rhi::ShaderStage::Compute, bpc);
            cmd->dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);
        }

        {
            rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_renderer->getTexture(m_ssaoIntermediate);
            barrier.srcAccessStage = rhi::ShaderStage::Compute;
            barrier.dstAccessStage = rhi::ShaderStage::Compute;
            barrier.oldLayout = rhi::ResourceLayout::General;
            barrier.newLayout = rhi::ResourceLayout::ShaderReadOnly;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Compute, barrier);

            bpc.inputTexID = ssaoIntermediateSampledIdx;
            bpc.outputTexID = util::u32(ssaoBlurIdx);
            bpc.axis = 1;
            cmd->pushConstants(rhi::ShaderStage::Compute, bpc);
            cmd->dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);
        }
    }

    void SSAOPass::resize(uint32_t width, uint32_t height,
                          const MSAASettings & ) {
      if (m_width == width && m_height == height) {
        return;
      }
      m_width = width;
      m_height = height;
      createResources(width, height);
    }

    void SSAOPass::execute([[maybe_unused]] const RenderPassContext& ctx)
    {
    }
}
