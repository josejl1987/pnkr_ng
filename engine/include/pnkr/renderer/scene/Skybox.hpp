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
        void init(RHIRenderer& renderer, TextureHandle cubemap);
        void initFromEquirectangular(RHIRenderer& renderer, const std::filesystem::path& path);

        void draw(rhi::RHICommandList* cmd, const Camera& camera);
        void resize(uint32_t msaaSamples);

        void destroy();
        [[nodiscard]] bool isValid() const;
        TextureHandle getTexture() const { return m_cubemapHandle; }
        void setFlipY(bool flipY) { m_flipY = flipY; }
        void setRotation(float rotationDeg) { m_rotation = rotationDeg; }
        float getRotation() const { return m_rotation; }

    private:
        void createSkyboxPipeline();

        RHIRenderer* m_renderer = nullptr;
        TextureHandle m_cubemapHandle{INVALID_TEXTURE_HANDLE};
        PipelinePtr m_pipeline;
        bool m_flipY = false;
        float m_rotation = 0.0f;
        uint32_t m_msaaSamples = 1;
    };
}
