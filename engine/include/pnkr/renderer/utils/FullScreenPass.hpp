#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/Handle.h"
#include <filesystem>

namespace pnkr::renderer::utils
{
    struct FullScreenPassConfig
    {
        std::filesystem::path fragmentShaderPath;
        rhi::Format colorFormat = rhi::Format::B8G8R8A8_UNORM; // Default to swapchain
        rhi::Format depthFormat = rhi::Format::Undefined;      // Default none
        
        bool enableBlending = false;
        bool enableDepthTest = false;
        bool enableDepthWrite = false;
        const char* debugName = "FullScreenPass";
    };

    class FullScreenPass
    {
    public:
        FullScreenPass() = default;
        ~FullScreenPass() = default;

        // Initialize the pass with a specific fragment shader
        void init(RHIRenderer& renderer, const FullScreenPassConfig& config);

        // Record the full screen draw call
        // Note: Caller must bind DescriptorSets/PushConstants before calling draw()
        void draw(rhi::RHICommandBuffer* cmd);

        [[nodiscard]] PipelineHandle pipeline() const { return m_pipeline; }

    private:
        RHIRenderer* m_renderer = nullptr;
        PipelineHandle m_pipeline = INVALID_PIPELINE_HANDLE;
    };
}
