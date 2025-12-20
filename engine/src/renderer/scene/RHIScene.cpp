#include "pnkr/renderer/scene/RHIScene.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/scene/Skybox.hpp"

namespace pnkr::renderer::scene {

    void RHIScene::update(float dt, int width, int height) {
        // Update camera controller
        (void)dt;
        // Note: This would need input from the application
        // m_cameraController.update(input, dt); // TODO: Pass input from application

        // Apply camera controller to camera
        m_cameraController.applyToCamera(m_camera);

        // Handle resize
        if (width != m_lastWidth || height != m_lastHeight) {
            // Update camera projection if needed
            m_lastWidth = width;
            m_lastHeight = height;
        }
    }

    void RHIScene::render(rhi::RHICommandBuffer* cmd) const {
        // Render skybox first (before opaque objects)
        if (m_skybox) {
            renderSkybox(cmd);
        }

        if (m_grid)
        {
            renderGrid(cmd);
        }

        // Render all objects
        for (const auto& obj : m_objects) {
            if (!obj.mesh || !obj.pipe) {
                continue;
            }

            // Bind pipeline
            m_renderer.bindPipeline(cmd, obj.pipe);

            // Bind mesh
            m_renderer.bindMesh(cmd, obj.mesh);

            // Draw mesh
            m_renderer.drawMesh(cmd, obj.mesh);
        }
    }

    void RHIScene::loadSkybox(const std::vector<std::filesystem::path>& faces) {
        m_skybox = std::make_unique<Skybox>();
        m_skybox->init(m_renderer, faces);

        if (m_skybox) {
            core::Logger::info("Skybox loaded successfully");
        } else {
            core::Logger::error("Failed to load skybox");
        }
    }

    void RHIScene::renderSkybox(rhi::RHICommandBuffer* cmd) const {
        if (m_skybox) {
            // Render skybox with camera
            m_skybox->draw(cmd,  m_camera);
        }
    }

    void RHIScene::initGrid()
    {
        m_grid = std::make_unique<InfiniteGrid>();
        m_grid->init(m_renderer);

    }

    void RHIScene::renderGrid(rhi::RHICommandBuffer* cmd) const
    {
        if (m_show_grid) {
            // Render skybox with camera
            m_grid->draw(cmd,  m_camera);
        }
    }

    void RHIScene::enableGrid(bool enable)
    {
        m_show_grid = enable;
    }
} // namespace pnkr::renderer::scene