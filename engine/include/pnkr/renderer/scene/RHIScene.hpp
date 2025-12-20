#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/core/Handle.h"
#include <vector>

#include "Skybox.hpp"

namespace pnkr::renderer::scene {

    struct RHIRenderable {
        Transform xform;
        MeshHandle mesh{};
        PipelineHandle pipe{};
    };

    class RHIScene {
    public:
        RHIScene(RHIRenderer& renderer) : m_renderer(renderer)
        {
            initGrid();
        }

        void update(float dt, int width, int height);
        void render(rhi::RHICommandBuffer* cmd) const;

        // Skybox functionality
        void loadSkybox(const std::vector<std::filesystem::path>& faces);
        void renderSkybox(rhi::RHICommandBuffer* cmd) const;

        // Grid functionality

        void initGrid();
        void renderGrid(rhi::RHICommandBuffer* cmd) const;
        void enableGrid(bool enable);

        Camera& camera() { return m_camera; }
        const Camera& camera() const { return m_camera; }

        CameraController& cameraController() { return m_cameraController; }
        const CameraController& cameraController() const { return m_cameraController; }

        std::vector<RHIRenderable>& objects() { return m_objects; }
        const std::vector<RHIRenderable>& objects() const { return m_objects; }

    private:
        RHIRenderer& m_renderer;
        Camera m_camera;
        CameraController m_cameraController;
        std::vector<RHIRenderable> m_objects;
        std::unique_ptr<Skybox> m_skybox;
        std::unique_ptr<InfiniteGrid> m_grid;

        int m_lastWidth = 0;
        int m_lastHeight = 0;
        bool m_show_grid = false;
    };

} // namespace pnkr::renderer::scene