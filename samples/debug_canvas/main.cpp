#include "pnkr/app/Application.hpp"
#include "pnkr/renderer/debug/LineCanvas3D.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/core/logger.hpp"
#include <imgui.h>

using namespace pnkr;

class DebugCanvasSample : public app::Application
{
public:
    DebugCanvasSample() : Application({
        .title = "Debug Canvas Sample", .width = 1280, .height = 720, .createRenderer = true
    })
    {
    }

    std::unique_ptr<renderer::debug::LineCanvas3D> m_canvas;
    renderer::scene::Camera m_camera;
    renderer::scene::CameraController m_cameraController;

    glm::vec3 m_cameraPosUI{0.0f};
    glm::vec3 m_cameraTargetUI{0.0f, 0.0f, -1.0f};

    void onInit() override
    {
        m_canvas = std::make_unique<renderer::debug::LineCanvas3D>();
        m_canvas->initialize(m_renderer.get());

        m_cameraController.setLookAt({0.0f, 5.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
        m_cameraController.applyToCamera(m_camera);
        m_camera.setPerspective(45.0f, (float)m_config.width / m_config.height, 0.1f, 1000.0f);

        m_cameraPosUI = m_cameraController.position();
        m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();

        pnkr::core::Logger::info("Debug Canvas Sample Initialized. Controls: WASD + Right Mouse.");
    }

    void onUpdate(float dt) override
    {
        m_cameraController.update(m_input, dt);
        m_cameraController.applyToCamera(m_camera);
    }

    void onEvent(const SDL_Event& event) override
    {
        (void)event;
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        m_canvas->beginFrame();

        m_canvas->plane({0, 0, 0}, {20, 0, 0}, {0, 0, 20}, 20, 20, {0.3f, 0.3f, 0.3f});

        m_canvas->line({0, 0, 0}, {2, 0, 0}, {1, 0, 0});
        m_canvas->line({0, 0, 0}, {0, 2, 0}, {0, 1, 0});
        m_canvas->line({0, 0, 0}, {0, 0, 2}, {0, 0, 1});

        m_canvas->box({-3, 0.5f, -3}, {-1, 2.5f, -1}, {1, 1, 0});
        m_canvas->circle({3, 0.0f, 0}, 1.5f, {0, 1, 0}, 64);
        m_canvas->sphere({0, 3, 0}, 1.0f, {1, 0, 1}, 32);

        glm::mat4 view = glm::lookAt(glm::vec3(5, 5, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.5f, 5.0f);
        m_canvas->frustum(proj * view, {1, 1, 1});

        m_canvas->endFrame();
        m_canvas->render(ctx, m_camera.viewProj());
    }

    void onImGui() override
    {
        ImGui::Begin("Camera");
        ImGui::Text("WASD + Right Mouse to move");
        ImGui::Separator();

        ImGui::InputFloat3("Position", &m_cameraPosUI.x);
        ImGui::InputFloat3("Target", &m_cameraTargetUI.x);
        if (ImGui::Button("Use Current"))
        {
            m_cameraPosUI = m_cameraController.position();
            m_cameraTargetUI = m_cameraPosUI + m_cameraController.front();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply"))
        {
            m_cameraController.setLookAt(m_cameraPosUI, m_cameraTargetUI);
            m_cameraController.applyToCamera(m_camera);
        }
        ImGui::End();
    }
};

int main(int, char**)
{
    DebugCanvasSample app;
    return app.run();
}
