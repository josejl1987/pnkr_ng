#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <filesystem>

#include "pnkr/app/Application.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/RHIScene.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"

using namespace pnkr;

class RHIGridApp : public app::Application
{
public:
    RHIGridApp()
        : app::Application({.title="RHI Skybox", .width=800, .height=600, .windowFlags=SDL_WINDOW_RESIZABLE, .createRenderer=false})
    {
    }

    void onInit() override
    {

        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        m_scene = std::make_unique<renderer::scene::RHIScene>(*m_renderer);

        auto& camera = m_scene->camera();
        float aspect = (float)m_window.width() / (float)m_window.height();
        camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);

        m_scene->cameraController().applyToCamera(camera);

        std::vector<std::filesystem::path> skyboxFaces = {
            "assets/skybox/posx.jpg",
            "assets/skybox/negx.jpg",
            "assets/skybox/posy.jpg",
            "assets/skybox/negy.jpg",
            "assets/skybox/posz.jpg",
            "assets/skybox/negz.jpg"
        };

        if (std::filesystem::exists(skyboxFaces[0])) {
            m_scene->loadSkybox(skyboxFaces);
        } else {
            core::Logger::warn("Skybox textures not found, will use procedural sky");

            std::vector<std::filesystem::path> emptyFaces(6, "");
            m_scene->loadSkybox(emptyFaces);
        }

        createSceneObjects();

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void onUpdate(float deltaTime) override
    {

        m_scene->cameraController().update(m_input, deltaTime);

        auto& camera = m_scene->camera();
        m_scene->cameraController().applyToCamera(camera);

        float aspect = (float)m_window.width() / (float)m_window.height();
        camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);
    }

    void createSceneObjects()
    {

    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        auto* cmd = ctx.commandBuffer;

        m_scene->update(ctx.deltaTime, m_window.width(), m_window.height());

        m_scene->render(cmd);
    }

private:
    std::unique_ptr<renderer::scene::RHIScene> m_scene;
};

int main(int , char* [])
{
    RHIGridApp app;
    return app.run();
}
