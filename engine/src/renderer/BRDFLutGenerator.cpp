#include "pnkr/renderer/BRDFLutGenerator.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include <cstddef>
#include <filesystem>
#include <ktx.h>

namespace pnkr::renderer
{
    struct BrdfPushConstants {
      uint32_t m_width;
      uint32_t m_height;
      uint64_t m_bufferAddress;
    };

    bool BRDFLutGenerator::generateAndSave(RHIRenderer* renderer, const std::string& outputPath, uint32_t width, uint32_t height, uint32_t numSamples)
    {
      if ((renderer == nullptr) || (renderer->device() == nullptr)) {
        core::Logger::Asset.error("BRDFLutGenerator: Invalid renderer");
        return false;
      }

        core::Logger::Asset.info("Generating BRDF LUT ({}x{}, {} samples)...", width, height, numSamples);

        auto outParams = rhi::TextureDescriptor{
            .type = rhi::TextureType::Texture2D,
            .extent = {.width = width, .height = height, .depth = 1},
            .format = rhi::Format::R16G16B16A16_SFLOAT,
            .usage = rhi::TextureUsage::Storage | rhi::TextureUsage::TransferSrc | rhi::TextureUsage::TransferDst,
            .mipLevels = 1,
            .arrayLayers = 1,
            .debugName = "BRDF_LUT_Texture"
        };

        auto dstTexture = renderer->device()->createTexture(outParams);

        if (!dstTexture) {
            core::Logger::Asset.error("BRDFLutGenerator: Failed to create texture");
            return false;
        }

        std::string shaderPath = "shaders/brdf_lut.spv";

        auto cs = rhi::Shader::load(rhi::ShaderStage::Compute, shaderPath);
        if (!cs) {
            core::Logger::Asset.error("BRDFLutGenerator: Failed to load compute shader from '{}'", shaderPath);
            return false;
        }

        auto pipeline = renderer->createComputePipeline(
            rhi::RHIPipelineBuilder()
                .setComputeShader(cs.get())
                .buildCompute()
        );

        auto* layout = renderer->getPipeline(pipeline)->descriptorSetLayout(0);
        auto descriptorSet = renderer->device()->allocateDescriptorSet(layout);

        if (descriptorSet) {
            descriptorSet->updateTexture(0, dstTexture.get(), nullptr);
            core::Logger::Asset.debug("BRDFLutGenerator: Bound texture to set 0 binding 0");
        } else {
             core::Logger::Asset.error("BRDFLutGenerator: Failed to allocate descriptor set. Shader might not reflect set 0.");
        }

        renderer->device()->immediateSubmit([&](rhi::RHICommandList* cmd) {

            std::vector<rhi::RHIMemoryBarrier> barriers(1);
            barriers[0].texture = dstTexture.get();
            barriers[0].oldLayout = rhi::ResourceLayout::Undefined;
            barriers[0].newLayout = rhi::ResourceLayout::General;
            barriers[0].srcAccessStage = rhi::ShaderStage::All;
            barriers[0].dstAccessStage = rhi::ShaderStage::Compute;
            cmd->pipelineBarrier(rhi::ShaderStage::All, rhi::ShaderStage::Compute, barriers);

            cmd->bindPipeline(renderer->getPipeline(pipeline));

            if (descriptorSet) {
                cmd->bindDescriptorSet(0, descriptorSet.get());
            }

            cmd->dispatch((width + 15) / 16, (height + 15) / 16, 1);

            barriers[0].oldLayout = rhi::ResourceLayout::General;
            barriers[0].newLayout = rhi::ResourceLayout::TransferSrc;
            barriers[0].srcAccessStage = rhi::ShaderStage::Compute;
            barriers[0].dstAccessStage = rhi::ShaderStage::Transfer;
            cmd->pipelineBarrier(rhi::ShaderStage::Compute, rhi::ShaderStage::Transfer, barriers);
        });

        const uint64_t bufferSize =
            static_cast<unsigned long long>(width * height * 4) *
            sizeof(uint16_t);
        std::vector<std::byte> cpuData(bufferSize);

        renderer->device()->downloadTexture(dstTexture.get(), std::span<std::byte>(cpuData.data(), cpuData.size()));

        constexpr ktx_uint32_t ktxVkFormatR16G16B16A16Sfloat = 97;

        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = ktxVkFormatR16G16B16A16Sfloat;
        createInfo.baseWidth = width;
        createInfo.baseHeight = height;
        createInfo.baseDepth = 1;
        createInfo.numDimensions = 2;
        createInfo.numLevels = 1;
        createInfo.numLayers = 1;
        createInfo.numFaces = 1;
        createInfo.isArray = KTX_FALSE;
        createInfo.generateMipmaps = KTX_FALSE;

        ktxTexture2* ktxTex = nullptr;
        bool success = false;

        if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktxTex) != KTX_SUCCESS)
        {
            core::Logger::Asset.error("BRDFLutGenerator: Failed to create KTX texture");
        }
        else if (ktxTexture_SetImageFromMemory(ktxTexture(ktxTex), 0, 0, 0,
                                               reinterpret_cast<const ktx_uint8_t*>(cpuData.data()),
                                               bufferSize) != KTX_SUCCESS)
        {
            core::Logger::Asset.error("BRDFLutGenerator: Failed to populate KTX texture");
            ktxTexture_Destroy(ktxTexture(ktxTex));
        }
        else
        {
            std::filesystem::path outPath(outputPath);
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);

            if (ktxTexture_WriteToNamedFile(ktxTexture(ktxTex), outputPath.c_str()) != KTX_SUCCESS)
            {
                core::Logger::Asset.error("BRDFLutGenerator: Failed to write '{}'", outputPath);
                ktxTexture_Destroy(ktxTexture(ktxTex));
            }
            else
            {
                core::Logger::Asset.info("BRDF LUT generated and saved to '{}'", outputPath);
                ktxTexture_Destroy(ktxTexture(ktxTex));
                success = true;
            }
        }

        return success;
        return success;
    }
}

