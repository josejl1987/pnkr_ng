#pragma once

#include "pnkr/renderer/RenderSettings.hpp"
#include "pnkr/renderer/passes/IRenderPass.hpp"

namespace pnkr::renderer {
    class RHIRenderer;

    class EnvironmentUploader {
    public:
        void upload(RHIRenderer& renderer,
                    const RenderGraphResources& resources,
                    RenderSettings& settings);

        uint32_t version() const { return m_environmentVersion; }

    private:
        uint32_t m_environmentVersion = 1;
    };
}
