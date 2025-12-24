#include "pnkr/engine.hpp"
#include "IndirectRenderer.hpp"
#include "../common/RhiSampleApp.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"

using namespace pnkr;

class IndirectSample : public samples::RhiSampleApp {
public:
    IndirectSample() : RhiSampleApp({.title = "Indirect Rendering - Bistro", .createRenderer = true}) {}

    std::shared_ptr<renderer::scene::ModelDOD> m_model;
    std::unique_ptr<indirect::IndirectRenderer> m_indirectRenderer;
    renderer::scene::Camera m_camera;
    renderer::scene::CameraController m_cameraController;

    void onInit() override {
        std::filesystem::path assetPath = baseDir() / "assets/Bistro.glb";
        
        pnkr::core::Logger::info("Loading model from: {}", assetPath.string());

        m_model = renderer::scene::ModelDOD::load(*m_renderer, assetPath);
        
        if (!m_model) {
            pnkr::core::Logger::error("Failed to load model");
            return;
        }

        // Init Indirect Renderer
        m_indirectRenderer = std::make_unique<indirect::IndirectRenderer>();
        m_indirectRenderer->init(m_renderer.get(), m_model);

        // Setup Camera
        m_cameraController.setPosition({0, 2, 0});
        m_cameraController.applyToCamera(m_camera);
        m_camera.setPerspective(glm::radians(60.0f), (float)m_config.width / m_config.height, 0.1f, 1000.0f);
    }

    void onUpdate(float dt) override {
        m_cameraController.update(m_input, dt);
        m_cameraController.applyToCamera(m_camera);

        if (m_indirectRenderer) {
            m_indirectRenderer->update(dt);
        }
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        if (m_indirectRenderer) {
            m_indirectRenderer->draw(ctx.commandBuffer, m_camera);
        }
    }
};

int main(int argc, char** argv) {
    IndirectSample app;
    return app.run();
}
