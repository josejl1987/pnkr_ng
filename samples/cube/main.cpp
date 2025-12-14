/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <filesystem>
#include <glm/common.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <pnkr/engine.hpp>

#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/geometry/mesh.h"
#include "pnkr/renderer/vulkan/geometry/Vertex.h"

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  constexpr int kLogFps = 60;
  constexpr float kDeltaTime = 0.1F;
  constexpr float kSize = 0.5F;

  std::vector<pnkr::renderer::Vertex> cubeVertices = {
      // +X (right) - red
      {.m_position = {+kSize, -kSize, -kSize}, .m_color = {1, 0, 0}},
      {.m_position = {+kSize, +kSize, -kSize}, .m_color = {1, 0, 0}},
      {.m_position = {+kSize, +kSize, +kSize}, .m_color = {1, 0, 0}},
      {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 0, 0}},

      // -X (left) - green
      {.m_position = {-kSize, -kSize, +kSize}, .m_color = {0, 1, 0}},
      {.m_position = {-kSize, +kSize, +kSize}, .m_color = {0, 1, 0}},
      {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 1, 0}},
      {.m_position = {-kSize, -kSize, -kSize}, .m_color = {0, 1, 0}},

      // +Y (top) - blue
      {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 0, 1}},
      {.m_position = {-kSize, +kSize, +kSize}, .m_color = {0, 0, 1}},
      {.m_position = {+kSize, +kSize, +kSize}, .m_color = {0, 0, 1}},
      {.m_position = {+kSize, +kSize, -kSize}, .m_color = {0, 0, 1}},

      // -Y (bottom) - yellow
      {.m_position = {-kSize, -kSize, +kSize}, .m_color = {1, 1, 0}},
      {.m_position = {-kSize, -kSize, -kSize}, .m_color = {1, 1, 0}},
      {.m_position = {+kSize, -kSize, -kSize}, .m_color = {1, 1, 0}},
      {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 1, 0}},

      // +Z (front) - magenta
      {.m_position = {-kSize, -kSize, +kSize}, .m_color = {1, 0, 1}},
      {.m_position = {+kSize, -kSize, +kSize}, .m_color = {1, 0, 1}},
      {.m_position = {+kSize, +kSize, +kSize}, .m_color = {1, 0, 1}},
      {.m_position = {-kSize, +kSize, +kSize}, .m_color = {1, 0, 1}},

      // -Z (back) - cyan
      {.m_position = {+kSize, -kSize, -kSize}, .m_color = {0, 1, 1}},
      {.m_position = {-kSize, -kSize, -kSize}, .m_color = {0, 1, 1}},
      {.m_position = {-kSize, +kSize, -kSize}, .m_color = {0, 1, 1}},
      {.m_position = {+kSize, +kSize, -kSize}, .m_color = {0, 1, 1}},
  };

  static const std::vector<std::uint32_t> cubeIndices = {
      0,  1,  2,  0,  2,  3,  // +X
      4,  5,  6,  4,  6,  7,  // -X
      8,  9,  10, 8,  10, 11, // +Y
      12, 13, 14, 12, 14, 15, // -Y
      16, 17, 18, 16, 18, 19, // +Z
      20, 21, 22, 20, 22, 23  // -Z
  };

  try {
    constexpr int kWindowHeight = 600;
    constexpr int kWindowWidth = 800;
    pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
    pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR,
                    PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

    const fs::path exePath =
        (argc > 0 && argv != nullptr) ? fs::path(argv[0]) : fs::current_path();
    const fs::path shaderDir = exePath.parent_path() / "shaders";

    if (!fs::exists(shaderDir)) {
      pnkr::Log::error("Shader directory not found: {}", shaderDir.string());
      return 1;
    }

    pnkr::renderer::RendererConfig rendererConfig{};
    rendererConfig.m_pipeline.m_vertSpvPath = shaderDir / "cube.vert.spv";
    rendererConfig.m_pipeline.m_fragSpvPath = shaderDir / "cube.frag.spv";

    if (!fs::exists(rendererConfig.m_pipeline.m_vertSpvPath)) {
      pnkr::Log::error("Vertex shader not found: {}",
                       rendererConfig.m_pipeline.m_vertSpvPath.string());
      return 1;
    }
    if (!fs::exists(rendererConfig.m_pipeline.m_fragSpvPath)) {
      pnkr::Log::error("Fragment shader not found: {}",
                       rendererConfig.m_pipeline.m_fragSpvPath.string());
      return 1;
    }
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();

    float deltaTime = 0.0f;
    pnkr::Window window("PNKR - Cube", kWindowWidth, kWindowHeight);
    pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

    pnkr::renderer::Renderer renderer(window, rendererConfig);
    MeshHandle cube = renderer.createMesh(cubeVertices, cubeIndices);
    pnkr::renderer::VulkanPipeline::Config cubeCfg{};
    cubeCfg.m_vertSpvPath = "shaders/cube.vert.spv";
    cubeCfg.m_fragSpvPath = "shaders/cube.frag.spv";
    cubeCfg.m_vertexInput = VertexInputDescription::VertexInputCube();

    PipelineHandle cubePipe = renderer.createPipeline(cubeCfg);

    pnkr::renderer::scene::Camera cam;
    cam.lookAt({1.5f, 1.2f, 1.5f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});

    vk::Extent2D lastExtent{0,0};

    renderer.setRecordFunc([&](const pnkr::renderer::RenderFrameContext& ctx) {
      if (ctx.m_extent.width != lastExtent.width || ctx.m_extent.height != lastExtent.height) {
        lastExtent = ctx.m_extent;
        const float aspect = float(lastExtent.width) / float(lastExtent.height);
        cam.setPerspective(glm::radians(60.0f), aspect, 0.1f, 10.0f);
      }

      static float timeVal = 0.0f;
      timeVal += ctx.m_deltaTime;

      pnkr::renderer::scene::Transform xform;
      xform.m_rotation.y = timeVal;

      PushConstants pc{};
      pc.m_model = xform.mat4();
      pc.m_viewProj = cam.viewProj();

      renderer.pushConstants(ctx.m_cmd, cubePipe, vk::ShaderStageFlagBits::eVertex, pc);
      renderer.bindPipeline(ctx.m_cmd, cubePipe);
      renderer.bindMesh(ctx.m_cmd, cube);
      renderer.drawMesh(ctx.m_cmd, cube);
    });

    int frameCount = 0;




    while (window.isRunning()) {
      try {
        window.processEvents();

        uint64_t now = SDL_GetPerformanceCounter();
        deltaTime = float(now - last) / float(freq);
        last = now;

        // avoid crazy jumps when resizing / breakpoints
        if (deltaTime > 0.05f) deltaTime = 0.05f;


        renderer.beginFrame(TODO);
        renderer.drawFrame();
        renderer.endFrame();

        if (++frameCount % kLogFps == 0) {
          pnkr::Log::debug("Running... (frames: {})", frameCount);
        }
      } catch (const std::exception &e) {
        pnkr::Log::error("Frame error: {}", e.what());
        break;
      }
    }

    pnkr::Log::info("Engine shutdown (rendered {} frames)", frameCount);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: " << e.what() << '\n';
    return 1;
  }
}
