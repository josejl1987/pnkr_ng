#include "pnkr/engine.hpp"
#include "IndirectRenderer.hpp"
#include "../common/RhiSampleApp.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"

using namespace pnkr;

class IndirectSample : public samples::RhiSampleApp {
public:
    IndirectSample() : RhiSampleApp({.title = "Indirect Rendering - Bistro", .createRenderer = true, .width = 1824, .height = 928}) {}

    std::shared_ptr<renderer::scene::ModelDOD> m_model;
    std::unique_ptr<indirect::IndirectRenderer> m_indirectRenderer;
    renderer::scene::Camera m_camera;
    scene::CameraController m_cameraController{{-19.2609997, 8.46500015, -7.31699991}, 20.801124201214570, -16.146098030003937f};
    TextureHandle m_brdfLut;
    TextureHandle m_irradiance;
    TextureHandle m_prefilter;
    void onInit() override {
        std::filesystem::path assetPath = baseDir() / "assets/Bistro.glb";
        
        pnkr::core::Logger::info("Loading model from: {}", assetPath.string());

        m_model = renderer::scene::ModelDOD::load(*m_renderer, assetPath);
        m_brdfLut = m_renderer->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_irradiance.ktx");
        m_prefilter = m_renderer->loadTextureKTX("assets/immenstadter_horn_2k_prefilter.ktx");
        if (!m_model) {
            pnkr::core::Logger::error("Failed to load model");
            return;
        }

        // Init Indirect Renderer
        m_indirectRenderer = std::make_unique<indirect::IndirectRenderer>();
        m_indirectRenderer->init(m_renderer.get(), m_model, m_brdfLut, m_irradiance, m_prefilter);

        // Setup Camera
        m_cameraController.applyToCamera(m_camera);
        m_camera.setPerspective(45.0f, (float)m_config.width / m_config.height, 0.1f, 1000.0f);
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
