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
        // 1. Init Renderer
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window);

        // 2. Create Scene
        m_scene = std::make_unique<renderer::scene::RHIScene>(*m_renderer);

        // 3. Setup Camera
        auto& camera = m_scene->camera();
        float aspect = (float)m_window.width() / (float)m_window.height();
        camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);

        // Apply initial camera controller position to camera
        m_scene->cameraController().applyToCamera(camera);

        // 4. Load Skybox
        // Note: You would need to provide actual cubemap face images
        // These should be 6 images in the order: +X, -X, +Y, -Y, +Z, -Z
        std::vector<std::filesystem::path> skyboxFaces = {
            "assets/skybox/posx.jpg",
            "assets/skybox/negx.jpg",
            "assets/skybox/posy.jpg",
            "assets/skybox/negy.jpg",
            "assets/skybox/posz.jpg",
            "assets/skybox/negz.jpg"
        };

        // Try to load skybox - fall back to procedural if files don't exist
        if (std::filesystem::exists(skyboxFaces[0])) {
            m_scene->loadSkybox(skyboxFaces);
        } else {
            core::Logger::warn("Skybox textures not found, will use procedural sky");
            // Create empty faces to get a valid cubemap handle for procedural sky
            std::vector<std::filesystem::path> emptyFaces(6, "");
            m_scene->loadSkybox(emptyFaces);
        }

        // 5. Create some simple objects in the scene
        createSceneObjects();

        // 6. Set Record Callback
        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            this->recordFrame(ctx);
        });
    }

    void onUpdate(float deltaTime) override
    {
        // Update camera controller with input
        m_scene->cameraController().update(m_input, deltaTime);

        // Apply camera controller to scene camera
        auto& camera = m_scene->camera();
        m_scene->cameraController().applyToCamera(camera);

        // Update camera aspect ratio when window resizes
        float aspect = (float)m_window.width() / (float)m_window.height();
        camera.setPerspective(glm::radians(60.0F), aspect, 0.1F, 100.0F);
    }

    void createSceneObjects()
    {
        // This would create some objects to render alongside the skybox
        // For now, this is a placeholder showing where you'd add meshes

        // Example: Create a cube in the center
        // auto cubeData = samples::GeometryUtils::getCube();
        // auto cubeMesh = m_renderer->createMesh(cubeData.vertices, cubeData.indices);
        //
        // renderer::scene::RHIRenderable cube;
        // cube.mesh = cubeMesh;
        // cube.xform.position = {0.0f, 0.0f, 0.0f};
        // cube.pipe = createObjectPipeline(); // Would need to create a pipeline
        //
        // m_scene->objects().push_back(cube);
    }

    void recordFrame(const renderer::RHIFrameContext& ctx)
    {
        auto* cmd = ctx.commandBuffer;

        // Update scene
        m_scene->update(ctx.deltaTime, m_window.width(), m_window.height());

        // Render scene (including skybox)
        m_scene->render(cmd);
    }

private:
    std::unique_ptr<renderer::scene::RHIScene> m_scene;
};

int main(int /*argc*/, char* /*argv*/[])
{
    RHIGridApp app;
    return app.run();
}
