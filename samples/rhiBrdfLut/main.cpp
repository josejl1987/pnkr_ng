#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "../common/RhiSampleApp.hpp"
#include <ktx.h>
#include "generated/brdf_lut.comp.h"

using namespace pnkr;

struct BrdfPushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t numSamples;
    uint32_t padding;
    uint64_t bufferAddress;
};

class BrdfLutApp : public samples::RhiSampleApp {
public:
    BrdfLutApp() : samples::RhiSampleApp({.title="BRDF LUT Generator", .createRenderer=true}) {}

    void onInit() override {
        const uint32_t width = 256;
        const uint32_t height = 256;
        const uint32_t numSamples = 1024;
        // 4 components * 2 bytes (float16)
        const uint64_t bufferSize = width * height * 4 * sizeof(uint16_t);

        // 1. Create Buffer for Readback
        auto dstBuffer = m_renderer->device()->createBuffer({
            .size = bufferSize,
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUToCPU, // Allows mapped reading
            .debugName = "BRDF_LUT_Buffer"
        });

        // 2. Setup Compute Pipeline
        auto cs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Compute, getShaderPath("brdf_lut.comp.spv"));
        auto pipeline = m_renderer->createComputePipeline(
            renderer::rhi::RHIPipelineBuilder().setComputeShader(cs.get()).buildCompute()
        );

        // 3. Dispatch Compute
        m_renderer->device()->immediateSubmit([&](renderer::rhi::RHICommandBuffer* cmd) {
            m_renderer->bindComputePipeline(cmd, pipeline);

            ShaderGen::constants pc = {width, height,  dstBuffer->getDeviceAddress()};

            cmd->pushConstants(m_renderer->getPipeline(pipeline), renderer::rhi::ShaderStage::Compute, 0, sizeof(pc), &pc);
            
            cmd->dispatch((width + 15) / 16, (height + 15) / 16, 1);
        });

        // 4. Save to disk
        void* data = dstBuffer->map();
        constexpr ktx_uint32_t KTX_VK_FORMAT_R16G16B16A16_SFLOAT = 97;

        ktxTextureCreateInfo createInfo{};
        createInfo.vkFormat = KTX_VK_FORMAT_R16G16B16A16_SFLOAT;
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
        if (ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktxTex) != KTX_SUCCESS)
        {
            pnkr::Log::error("Failed to create KTX texture for BRDF LUT");
        }
        else if (ktxTexture_SetImageFromMemory(ktxTexture(ktxTex), 0, 0, 0,
                                               reinterpret_cast<const ktx_uint8_t*>(data),
                                               bufferSize) != KTX_SUCCESS)
        {
            pnkr::Log::error("Failed to populate KTX texture for BRDF LUT");
            ktxTexture_Destroy(ktxTexture(ktxTex));
        }
        else if (ktxTexture_WriteToNamedFile(ktxTexture(ktxTex), "brdf_lut.ktx2") != KTX_SUCCESS)
        {
            pnkr::Log::error("Failed to write 'brdf_lut.ktx2'");
            ktxTexture_Destroy(ktxTexture(ktxTex));
        }
        else
        {
            pnkr::Log::info("BRDF LUT generated and saved to 'brdf_lut.ktx2'");
            ktxTexture_Destroy(ktxTexture(ktxTex));
        }
        dstBuffer->unmap();
        
        // Signal app to close
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
};

int main(int argc, char** argv) {
    BrdfLutApp app;
    return app.run();
}
