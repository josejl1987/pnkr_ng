#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include <vector>
#include <filesystem>
#include <glm/mat4x4.hpp>

namespace pnkr::renderer::scene {

    class Skybox {
    public:

        void init(RHIRenderer& renderer, const std::vector<std::filesystem::path>& faces);

        void draw(rhi::RHICommandBuffer* cmd, const Camera& camera);

        void destroy();

    private:
        void createSkyboxPipeline();

        RHIRenderer* m_renderer = nullptr; // Stored reference
        TextureHandle m_cubemapHandle{INVALID_TEXTURE_HANDLE};
        PipelineHandle m_pipeline{INVALID_PIPELINE_HANDLE};
    };
}