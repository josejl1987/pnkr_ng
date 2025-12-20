#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <vector>
#include <filesystem>
#include <glm/mat4x4.hpp>

namespace pnkr::renderer::scene {



    class Skybox {
    public:
        // Changed: Removed the manual loadSpirv helper (we use RHI Factory)
        // We store the renderer pointer in init.
        void init(RHIRenderer& renderer, const std::vector<std::filesystem::path>& faces);

        // Changed: Renderer is no longer a parameter
        void draw(rhi::RHICommandBuffer* cmd, const Camera& camera);

        // Cleanup resources
        void destroy();

    private:
        void createSkyboxPipeline();

        RHIRenderer* m_renderer = nullptr; // Stored reference
        TextureHandle m_cubemapHandle{INVALID_TEXTURE_HANDLE};
        PipelineHandle m_pipeline{INVALID_PIPELINE_HANDLE};
    };
}