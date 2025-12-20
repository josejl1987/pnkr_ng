#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <vector>
#include <filesystem>
#include <glm/mat4x4.hpp>

namespace pnkr::renderer::scene {

    class InfiniteGrid {
    public:

        void init(RHIRenderer& renderer);

        void draw(rhi::RHICommandBuffer* cmd, const Camera& camera) const;

        void destroy();

    private:
        void createGridPipeline();

        RHIRenderer* m_renderer = nullptr; // Stored reference
        PipelineHandle m_pipeline{INVALID_PIPELINE_HANDLE};
    };
}