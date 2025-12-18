#include "../common/RhiSampleApp.hpp"
#include "pnkr/renderer/debug/DebugLayer.hpp"
#include "pnkr/renderer/scene/RHIScene.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace pnkr;
using namespace pnkr::samples;



class DebugCanvasSample : public RhiSampleApp
{
public:
    DebugCanvasSample() : RhiSampleApp({
        .title = "PNKR DebugLayer Sample",
        .width = 1280,
        .height = 720
    })
    {
    }

protected:
    void onInit() override
    {
        // Initialize debug layer
        m_debugLayer.initialize(m_renderer.get());

        // Configure debug layer for overlay-style rendering (always visible)
        m_debugLayer.setDepthTestEnabled(false);

        // Initialize scene
        m_scene = std::make_unique<renderer::scene::RHIScene>(*m_renderer);

        // Set up camera controller (uses built-in FPS controls)
        auto& cameraController = m_scene->cameraController();
        cameraController.setPosition(glm::vec3(0.0f, 5.0f, 15.0f));

        // Set up camera
        auto& camera = m_scene->camera();
        camera.setPerspective(glm::radians(45.0f),
                             static_cast<float>(m_config.width) / m_config.height,
                             0.1f, 1000.0f);

        // Set scene reference for debug layer (optional)
        m_debugLayer.setScene(m_scene.get());

        // Start rotation timer
        m_rotation = 0.0f;
    }

    void onUpdate(float dt) override
    {
        // Update rotation
        m_rotation += dt * 0.5f;

        // Update scene with input (camera controller handles input)
        auto& cameraController = m_scene->cameraController();
        cameraController.update(m_input, dt);

        // Apply camera controller to camera
        cameraController.applyToCamera(m_scene->camera());

        // Debug layer doesn't need update anymore - it's cleared each frame

        // Generate debug drawings for this frame
        generateDebugDrawing();


    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        // Render the scene first
        m_scene->render(ctx.commandBuffer);

        // Then render debug layer on top with scene's view-projection matrix
        glm::mat4 viewProj = m_scene->camera().viewProj();
        m_debugLayer.render(ctx, viewProj);
    }

    void generateDebugDrawing()
    {
        // Note: Camera controls - WASD to move, Right Mouse to look around, Shift to speed up, Q/E for up/down
        // Note: DebugLayer now handles clearing internally after render() to prevent race conditions

        // Draw coordinate axes at origin
        m_debugLayer.line(glm::vec3(0, 0, 0), glm::vec3(5, 0, 0), glm::vec3(1, 0, 0)); // X - Red
        m_debugLayer.line(glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), glm::vec3(0, 1, 0)); // Y - Green
        m_debugLayer.line(glm::vec3(0, 0, 0), glm::vec3(0, 0, 5), glm::vec3(0, 0, 1)); // Z - Blue

        // Draw a rotating box
        glm::mat4 boxTransform = glm::mat4(1.0f);
        boxTransform = glm::rotate(boxTransform, m_rotation, glm::vec3(0, 1, 0));
        boxTransform = glm::rotate(boxTransform, m_rotation * 0.7f, glm::vec3(1, 0, 0));
        m_debugLayer.box(boxTransform, glm::vec3(2.0f), glm::vec3(1, 1, 0));

        // Draw a sphere
        m_debugLayer.sphere(glm::vec3(4, 0, 0), 1.0f, glm::vec3(0, 1, 1), 16);

        // Draw a circle in YZ plane (normal points along X-axis)
        m_debugLayer.circle(glm::vec3(-4, 0, 0), 1.5f, glm::vec3(1, 0, 0), glm::vec3(1, 0, 1), 32);

        // Draw a plane grid (slightly offset to prevent z-fighting)
        m_debugLayer.plane(glm::vec3(0, -2.99f, 0),  // Small offset to prevent z-fighting
                          glm::vec3(20, 0, 0),
                          glm::vec3(0, 0, 20),
                          20, 20,
                          glm::vec3(0.3f, 0.3f, 0.3f));

        // Draw multiple boxes in a grid pattern
        for (int x = -2; x <= 2; ++x)
        {
            for (int z = -2; z <= 2; ++z)
            {
                if (x == 0 && z == 0) continue; // Skip center

                glm::vec3 pos(x * 3.0f, 2.0f, z * 3.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
                transform = glm::rotate(transform, m_rotation + static_cast<float>(x + z),
                                       glm::vec3(0, 1, 0));
                transform = glm::scale(transform, glm::vec3(0.5f));

                glm::vec3 color = glm::vec3(
                    (static_cast<float>(x + 2) / 4.0f),
                    (static_cast<float>(z + 2) / 4.0f),
                    0.5f
                );

                m_debugLayer.box(transform, glm::vec3(1.0f), color);
            }
        }

        // Draw a frustum (visualize a camera)
        glm::mat4 frustumView = glm::lookAt(glm::vec3(-8, 5, 8), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 frustumProj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
        m_debugLayer.frustum(frustumProj * frustumView, glm::vec3(1, 1, 0));

        // Draw a wireframe frame around the scene
        glm::vec3 boundsMin = glm::vec3(-8, -3, -8);
        glm::vec3 boundsMax = glm::vec3(8, 5, 8);
        m_debugLayer.box(boundsMin, boundsMax, glm::vec3(0.5f));
    }

private:
    renderer::debug::DebugLayer m_debugLayer;
    std::unique_ptr<renderer::scene::RHIScene> m_scene;
    float m_rotation;
};

int main()
{
    DebugCanvasSample sample;
    return sample.run();
}