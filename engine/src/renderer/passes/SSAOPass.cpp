#include "pnkr/renderer/passes/SSAOPass.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include "pnkr/renderer/gpu_shared/PostProcessShared.h"
#include "pnkr/core/common.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <random>

namespace pnkr::renderer
{
    void SSAOPass::init(RHIRenderer* renderer, uint32_t width, uint32_t height)
    {
        m_renderer = renderer;
        m_width = width;
        m_height = height;

        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
        std::default_random_engine generator;
        std::vector<glm::vec4> ssaoNoise;
        for (uint32_t i = 0; i < 16; i++) {
          glm::vec3 noise((randomFloats(generator) * 2.0) - 1.0,
                          (randomFloats(generator) * 2.0) - 1.0,
                          (randomFloats(generator) * 2.0) - 1.0);
          noise = glm::normalize(noise);
          ssaoNoise.emplace_back(noise, 0.0F);
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
        m_depthResolvePipeline = m_renderer->createComputePipeline(builder.setComputeShader(sResolve.get()).setName("DepthResolve").buildCompute());

        auto sGen = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/ssao.spv");
        m_ssaoPipeline = m_renderer->createComputePipeline(builder.setComputeShader(sGen.get()).setName("SSAOGen").buildCompute());

        auto sBlur = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/ssao_blur.spv");
        m_blurPipeline = m_renderer->createComputePipeline(builder.setComputeShader(sBlur.get()).setName("SSAOBlur").buildCompute());

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
            std::vector<rhi::RHIMemoryBarrier> barriers(1);
            barriers[0].texture = m_renderer->getTexture(m_ssaoIntermediate);
            barriers[0].srcAccessStage = rhi::ShaderStage::Compute;
            barriers[0].dstAccessStage = rhi::ShaderStage::Compute;
            barriers[0].oldLayout = rhi::ResourceLayout::Undefined;
            barriers[0].newLayout = rhi::ResourceLayout::General;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Compute, barriers);

            bpc.inputTexID = util::u32(ssaoRawIdx);
            bpc.outputTexID = ssaoIntermediateStorageIdx;
            bpc.axis = 0;
            cmd->pushConstants(rhi::ShaderStage::Compute, bpc);
            cmd->dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1);
        }

        {
            std::vector<rhi::RHIMemoryBarrier> barriers(1);
            barriers[0].texture = m_renderer->getTexture(m_ssaoIntermediate);
            barriers[0].srcAccessStage = rhi::ShaderStage::Compute;
            barriers[0].dstAccessStage = rhi::ShaderStage::Compute;
            barriers[0].oldLayout = rhi::ResourceLayout::General;
            barriers[0].newLayout = rhi::ResourceLayout::ShaderReadOnly;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Compute, barriers);

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
