#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/Handle.h"
#include <filesystem>
#include <string>

namespace pnkr::renderer::utils
{
    struct FullScreenPassConfig
    {
        std::filesystem::path fragmentShaderPath;
        rhi::Format colorFormat = rhi::Format::B8G8R8A8_UNORM;
        rhi::Format depthFormat = rhi::Format::Undefined;

        bool enableBlending = false;
        bool enableDepthTest = false;
        bool enableDepthWrite = false;
        std::string debugName = "FullScreenPass";
    };

    class FullScreenPass
    {
    public:
        FullScreenPass() = default;
        ~FullScreenPass() = default;

        void init(RHIRenderer& renderer, const FullScreenPassConfig& config);

        void draw(rhi::RHICommandList *cmd) const;

        RHIRenderer* m_renderer = nullptr;
        PipelinePtr m_pipeline;
    };
}
