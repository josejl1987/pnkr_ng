/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <pnkr/engine.hpp>
#include <filesystem>
#include <iostream>
#include <glm/trigonometric.hpp>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/geometry/Mesh.h"
#include "pnkr/renderer/vulkan/geometry/Vertex.h"

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;

    static const std::vector<pnkr::renderer::Vertex> cubeVertices = {
        // +X (right) — red
        {{+0.5f, -0.5f, -0.5f}, {1, 0, 0}},
        {{+0.5f, +0.5f, -0.5f}, {1, 0, 0}},
        {{+0.5f, +0.5f, +0.5f}, {1, 0, 0}},
        {{+0.5f, -0.5f, +0.5f}, {1, 0, 0}},

        // -X (left) — green
        {{-0.5f, -0.5f, +0.5f}, {0, 1, 0}},
        {{-0.5f, +0.5f, +0.5f}, {0, 1, 0}},
        {{-0.5f, +0.5f, -0.5f}, {0, 1, 0}},
        {{-0.5f, -0.5f, -0.5f}, {0, 1, 0}},

        // +Y (top) — blue
        {{-0.5f, +0.5f, -0.5f}, {0, 0, 1}},
        {{-0.5f, +0.5f, +0.5f}, {0, 0, 1}},
        {{+0.5f, +0.5f, +0.5f}, {0, 0, 1}},
        {{+0.5f, +0.5f, -0.5f}, {0, 0, 1}},

        // -Y (bottom) — yellow
        {{-0.5f, -0.5f, +0.5f}, {1, 1, 0}},
        {{-0.5f, -0.5f, -0.5f}, {1, 1, 0}},
        {{+0.5f, -0.5f, -0.5f}, {1, 1, 0}},
        {{+0.5f, -0.5f, +0.5f}, {1, 1, 0}},

        // +Z (front) — magenta
        {{-0.5f, -0.5f, +0.5f}, {1, 0, 1}},
        {{+0.5f, -0.5f, +0.5f}, {1, 0, 1}},
        {{+0.5f, +0.5f, +0.5f}, {1, 0, 1}},
        {{-0.5f, +0.5f, +0.5f}, {1, 0, 1}},

        // -Z (back) — cyan
        {{+0.5f, -0.5f, -0.5f}, {0, 1, 1}},
        {{-0.5f, -0.5f, -0.5f}, {0, 1, 1}},
        {{-0.5f, +0.5f, -0.5f}, {0, 1, 1}},
        {{+0.5f, +0.5f, -0.5f}, {0, 1, 1}},
    };

    static const std::vector<std::uint32_t> cubeIndices = {
        0, 1, 2, 0, 2, 3, // +X
        4, 5, 6, 4, 6, 7, // -X
        8, 9, 10, 8, 10, 11, // +Y
        12, 13, 14, 12, 14, 15, // -Y
        16, 17, 18, 16, 18, 19, // +Z
        20, 21, 22, 20, 22, 23 // -Z
    };


    try
    {
        pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
        pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR, PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

        const fs::path exePath = (argc > 0 && argv) ? fs::path(argv[0]) : fs::current_path();
        const fs::path shaderDir = exePath.parent_path() / "shaders";

        if (!fs::exists(shaderDir))
        {
            pnkr::Log::error("Shader directory not found: {}", shaderDir.string());
            return 1;
        }

        pnkr::renderer::RendererConfig renderer_config{};
        renderer_config.pipeline.vertSpvPath = shaderDir / "cube.vert.spv";
        renderer_config.pipeline.fragSpvPath = shaderDir / "cube.frag.spv";

        if (!fs::exists(renderer_config.pipeline.vertSpvPath))
        {
            pnkr::Log::error("Vertex shader not found: {}", renderer_config.pipeline.vertSpvPath.string());
            return 1;
        }
        if (!fs::exists(renderer_config.pipeline.fragSpvPath))
        {
            pnkr::Log::error("Fragment shader not found: {}", renderer_config.pipeline.fragSpvPath.string());
            return 1;
        }

        pnkr::Window window("PNKR - Cube", 800, 600);
        pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

        pnkr::renderer::Renderer renderer(window, renderer_config);
        MeshHandle cube = renderer.createMesh(cubeVertices, cubeIndices);
        VertexInputDescription vertexInput;
        pnkr::renderer::VulkanPipeline::Config cubeCfg{};
        cubeCfg.vertSpvPath = "shaders/cube.vert.spv";
        cubeCfg.fragSpvPath = "shaders/cube.frag.spv";
        cubeCfg.vertexInput = VertexInputDescription::VertexInputCube();

        PipelineHandle cubePipe = renderer.createPipeline(cubeCfg);

        float deltaSeconds = 0.1;
        renderer.setRecordFunc([&](pnkr::renderer::RenderFrameContext& ctx) {
            static float t = 0.0f;
            t += deltaSeconds; // however you track time

            PushConstants pc{};
            pc.model = glm::rotate(glm::mat4(1.0f), t, glm::vec3(0, 1, 0));

            glm::vec3 eye(1.5f, 1.2f, 1.5f);
            glm::vec3 center(0.0f, 0.0f, 0.0f);
            glm::vec3 up(0.0f, 1.0f, 0.0f);

            glm::mat4 view = glm::lookAt(eye, center, up);

            float aspect = float(ctx.extent.width) / float(ctx.extent.height);
            glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 10.0f);

            // Vulkan clip space has inverted Y compared to GLM’s default conventions:
            proj[1][1] *= -1.0f;

            pc.viewProj = proj * view;

            ctx.cmd.pushConstants(
              renderer.pipelineLayout(cubePipe), // see note below
              vk::ShaderStageFlagBits::eVertex,
              0,
              sizeof(PushConstants),
              &pc
            );
            renderer.bindPipeline(ctx.cmd, cubePipe);
            renderer.bindMesh(ctx.cmd, cube);
            renderer.drawMesh(ctx.cmd, cube);
        });
        int frame_count = 0;
        while (window.isRunning())
        {
            try
            {
                window.processEvents();

                renderer.beginFrame();
                renderer.drawFrame();
                renderer.endFrame();

                if (++frame_count % 60 == 0)
                {
                    pnkr::Log::debug("Running... (frames: {})", frame_count);
                }
            }
            catch (const std::exception& e)
            {
                pnkr::Log::error("Frame error: {}", e.what());
                break;
            }
        }

        pnkr::Log::info("Engine shutdown (rendered {} frames)", frame_count);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}
