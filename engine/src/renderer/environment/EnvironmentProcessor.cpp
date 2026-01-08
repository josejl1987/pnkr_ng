#include "pnkr/renderer/environment/EnvironmentProcessor.hpp"
#include "pnkr/renderer/gpu_shared/EnvironmentShared.h"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/core/logger.hpp"
#include <cmath>
#include <algorithm>

namespace pnkr::renderer {

    EnvironmentProcessor::EnvironmentProcessor(RHIRenderer* renderer) : m_renderer(renderer) {
        initPipelines();
    }

    void EnvironmentProcessor::initPipelines() {
        rhi::RHIPipelineBuilder builder;

        auto sBRDF = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/brdf_lut.spv");
        m_brdfPipeline = m_renderer->createComputePipeline(
            builder.setComputeShader(sBRDF.get()).setName("IBL_BRDF_LUT").buildCompute()
        ).release();

        auto sIrr = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/irradiance.spv");
        m_irradiancePipeline = m_renderer->createComputePipeline(
            builder.setComputeShader(sIrr.get()).setName("IBL_Irradiance").buildCompute()
        ).release();

        auto sPref = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/prefilter.spv");
        m_prefilterPipeline = m_renderer->createComputePipeline(
            builder.setComputeShader(sPref.get()).setName("IBL_Prefilter").buildCompute()
        ).release();

        auto sEqui = rhi::Shader::load(rhi::ShaderStage::Compute, "shaders/equirectangular_to_cubemap.spv");
        m_equiToCubePipeline = m_renderer->createComputePipeline(
            builder.setComputeShader(sEqui.get()).setName("EquiToCube").buildCompute()
        ).release();
    }

    TextureHandle EnvironmentProcessor::generateBRDFLUT() {
        uint32_t dim = 512;
        rhi::TextureDescriptor desc{};
        desc.extent = {.width = dim, .height = dim, .depth = 1};
        desc.format = rhi::Format::R16G16_SFLOAT;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        desc.debugName = "BRDF_LUT";

        TextureHandle lut = m_renderer->createTexture(desc).release();

        m_renderer->device()->immediateSubmit([&](rhi::RHICommandList* cmd) {
            rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_renderer->getTexture(lut);
            barrier.oldLayout = rhi::ResourceLayout::Undefined;
            barrier.newLayout = rhi::ResourceLayout::General;
            barrier.srcAccessStage = rhi::ShaderStage::None;
            barrier.dstAccessStage = rhi::ShaderStage::Compute;
            cmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::Compute, {barrier});

            auto* pipe = m_renderer->getPipeline(m_brdfPipeline);
            auto* layout = pipe->descriptorSetLayout(0);
            auto set = m_renderer->device()->allocateDescriptorSet(layout);
            set->updateTexture(0, m_renderer->getTexture(lut), nullptr);
            cmd->bindPipeline(pipe);
            cmd->bindDescriptorSet(0, set.get());

            cmd->dispatch((dim + 15) / 16, (dim + 15) / 16, 1);

            barrier.oldLayout = rhi::ResourceLayout::General;
            barrier.newLayout = rhi::ResourceLayout::ShaderReadOnly;
            barrier.srcAccessStage = rhi::ShaderStage::Compute;
            barrier.dstAccessStage = rhi::ShaderStage::Fragment;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Fragment, {barrier});
        });

        return lut;
    }

    GeneratedIBL EnvironmentProcessor::processEnvironment(TextureHandle skybox, bool flipY) {
        GeneratedIBL result;

        uint32_t irrDim = 32;
        rhi::TextureDescriptor irrDesc{};
        irrDesc.type = rhi::TextureType::TextureCube;
        irrDesc.extent = {.width = irrDim, .height = irrDim, .depth = 1};
        irrDesc.format = rhi::Format::R16G16B16A16_SFLOAT;
        irrDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        irrDesc.arrayLayers = 6;
        irrDesc.mipLevels = 1;
        irrDesc.debugName = "IrradianceMap_Generated";
        result.irradianceMap = m_renderer->createTexture(irrDesc).release();

        uint32_t preDim = 512;
        uint32_t preMips = static_cast<uint32_t>(std::floor(std::log2(preDim))) + 1;
        rhi::TextureDescriptor preDesc = irrDesc;
        preDesc.extent = {.width = preDim, .height = preDim, .depth = 1};
        preDesc.mipLevels = preMips;
        preDesc.debugName = "PrefilterMap_Generated";
        result.prefilteredMap = m_renderer->createTexture(preDesc).release();

        m_renderer->device()->immediateSubmit([&](rhi::RHICommandList* cmd) {
            auto barrier = [&](TextureHandle h, rhi::ResourceLayout oldL, rhi::ResourceLayout newL, uint32_t baseMip, uint32_t mipCount) {
                rhi::RHIMemoryBarrier b{};
                b.texture = m_renderer->getTexture(h);
                b.oldLayout = oldL;
                b.newLayout = newL;
                b.srcAccessStage = rhi::ShaderStage::Compute;
                b.dstAccessStage = rhi::ShaderStage::Compute;
                b.baseMipLevel = baseMip;
                b.levelCount = mipCount;
                b.layerCount = 6;
                cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Compute, {b});
            };

            barrier(result.irradianceMap, rhi::ResourceLayout::Undefined, rhi::ResourceLayout::General, 0, 1);
            barrier(result.prefilteredMap, rhi::ResourceLayout::Undefined, rhi::ResourceLayout::General, 0, preMips);

            gpu::EnvironmentPushConstants pc{};
            pc.envMapIndex = static_cast<uint32_t>(m_renderer->getTextureBindlessIndex(skybox));
            pc.samplerIndex = static_cast<uint32_t>(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
            pc.flipY = flipY ? 1U : 0U;

            {
                rhi::TextureViewDescriptor viewDesc{};
                viewDesc.format = rhi::Format::R16G16B16A16_SFLOAT;
                viewDesc.layerCount = 6;
                viewDesc.mipCount = 1;
                auto viewH = m_renderer->createTextureView(result.irradianceMap, viewDesc).release();

                auto* pipe = m_renderer->getPipeline(m_irradiancePipeline);
                auto set = m_renderer->device()->allocateDescriptorSet(pipe->descriptorSetLayout(0));
                set->updateTexture(0, m_renderer->getTexture(viewH), nullptr);

                cmd->bindPipeline(pipe);
                cmd->bindDescriptorSet(0, set.get());
                if (m_renderer->isBindlessEnabled()) {
                    cmd->bindDescriptorSet(1, m_renderer->device()->getBindlessDescriptorSet());
                }

                cmd->pushConstants(rhi::ShaderStage::Compute, pc);
                cmd->dispatch((irrDim + 7) / 8, (irrDim + 7) / 8, 6);

                m_renderer->destroyTexture(viewH);
            }

            auto* pipePre = m_renderer->getPipeline(m_prefilterPipeline);
            for (uint32_t m = 0; m < preMips; ++m) {
              uint32_t mipW = std::max(1U, preDim >> m);
              uint32_t mipH = std::max(1U, preDim >> m);

              pc.roughness = (float)m / (float)(preMips - 1);

              rhi::TextureViewDescriptor viewDesc{};
              viewDesc.mipLevel = m;
              viewDesc.mipCount = 1;
              viewDesc.layerCount = 6;
              viewDesc.format = rhi::Format::R16G16B16A16_SFLOAT;
              auto viewH =
                  m_renderer->createTextureView(result.prefilteredMap, viewDesc)
                      .release();

              auto set = m_renderer->device()->allocateDescriptorSet(
                  pipePre->descriptorSetLayout(0));
              set->updateTexture(0, m_renderer->getTexture(viewH), nullptr);

              cmd->bindPipeline(pipePre);
              cmd->bindDescriptorSet(0, set.get());
              if (m_renderer->isBindlessEnabled()) {
                cmd->bindDescriptorSet(
                    1, m_renderer->device()->getBindlessDescriptorSet());
              }

                cmd->pushConstants(rhi::ShaderStage::Compute, pc);
                cmd->dispatch((mipW + 7) / 8, (mipH + 7) / 8, 6);

                m_renderer->destroyTexture(viewH);
            }

            barrier(result.irradianceMap, rhi::ResourceLayout::General, rhi::ResourceLayout::ShaderReadOnly, 0, 1);
            barrier(result.prefilteredMap, rhi::ResourceLayout::General, rhi::ResourceLayout::ShaderReadOnly, 0, preMips);
        });

        return result;
    }

    TextureHandle EnvironmentProcessor::convertEquirectangularToCubemap(TextureHandle equiTex, uint32_t size) {
        rhi::TextureDescriptor desc{};
        desc.type = rhi::TextureType::TextureCube;
        desc.extent = {.width = size, .height = size, .depth = 1};
        desc.format = rhi::Format::R16G16B16A16_SFLOAT;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst;
        desc.arrayLayers = 6;
        desc.mipLevels = 1;
        desc.debugName = "SkyboxCubemap_Converted";
        TextureHandle cubemap = m_renderer->createTexture(desc).release();

        m_renderer->device()->immediateSubmit([&](rhi::RHICommandList* cmd) {
            rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_renderer->getTexture(cubemap);
            barrier.oldLayout = rhi::ResourceLayout::Undefined;
            barrier.newLayout = rhi::ResourceLayout::General;
            barrier.srcAccessStage = rhi::ShaderStage::None;
            barrier.dstAccessStage = rhi::ShaderStage::Compute;
            barrier.layerCount = 6;
            cmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::Compute, {barrier});

            auto* pipe = m_renderer->getPipeline(m_equiToCubePipeline);

            rhi::TextureViewDescriptor viewDesc{};
            viewDesc.layerCount = 6;
            viewDesc.format = rhi::Format::R16G16B16A16_SFLOAT;
            auto cubeView = m_renderer->createTextureView(cubemap, viewDesc).release();

            gpu::EquirectangularToCubemapPushConstants pc{};
            pc.envMapIndex = static_cast<uint32_t>(m_renderer->getTextureBindlessIndex(equiTex));
            pc.samplerIndex = static_cast<uint32_t>(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
            pc.targetCubeIndex = static_cast<uint32_t>(m_renderer->getStorageImageBindlessIndex(cubeView));

            core::Logger::Render.debug("EquiToCube: envMapIdx={}, targetCubeIdx={} (Storage)", pc.envMapIndex, pc.targetCubeIndex);

            cmd->bindPipeline(pipe);

            if (m_renderer->isBindlessEnabled()) {
                cmd->bindDescriptorSet(1, m_renderer->device()->getBindlessDescriptorSet());
            }

            cmd->pushConstants(rhi::ShaderStage::Compute, pc);
            cmd->dispatch((size + 7) / 8, (size + 7) / 8, 6);

            m_renderer->destroyTexture(cubeView);

            barrier.oldLayout = rhi::ResourceLayout::General;
            barrier.newLayout = rhi::ResourceLayout::ShaderReadOnly;
            barrier.srcAccessStage = rhi::ShaderStage::Compute;
            barrier.dstAccessStage = rhi::ShaderStage::Fragment;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Fragment, {barrier});
        });

        return cubemap;
    }
}
