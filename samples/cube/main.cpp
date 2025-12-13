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

int main(int argc, char** argv) {
  namespace fs = std::filesystem;

  try {
    pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
    pnkr::Log::info("PNKR Engine v{}.{}.{}", PNKR_VERSION_MAJOR, PNKR_VERSION_MINOR, PNKR_VERSION_PATCH);

    const fs::path exePath = (argc > 0 && argv) ? fs::path(argv[0]) : fs::current_path();
    const fs::path shaderDir = exePath.parent_path() / "shaders";

    if (!fs::exists(shaderDir)) {
      pnkr::Log::error("Shader directory not found: {}", shaderDir.string());
      return 1;
    }

    pnkr::renderer::RendererConfig renderer_config{};
    renderer_config.pipeline.vertSpvPath = shaderDir / "cube.vert.spv";
    renderer_config.pipeline.fragSpvPath = shaderDir / "cube.frag.spv";

    if (!fs::exists(renderer_config.pipeline.vertSpvPath)) {
      pnkr::Log::error("Vertex shader not found: {}", renderer_config.pipeline.vertSpvPath.string());
      return 1;
    }
    if (!fs::exists(renderer_config.pipeline.fragSpvPath)) {
      pnkr::Log::error("Fragment shader not found: {}", renderer_config.pipeline.fragSpvPath.string());
      return 1;
    }

    pnkr::Window window("PNKR - Cube", 800, 600);
    pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

    pnkr::renderer::Renderer renderer(window, renderer_config);

    int frame_count = 0;
    while (window.isRunning()) {
      try {
        window.processEvents();

        renderer.beginFrame();
        renderer.drawFrame();
        renderer.endFrame();

        if (++frame_count % 60 == 0) {
          pnkr::Log::debug("Running... (frames: {})", frame_count);
        }
      } catch (const std::exception& e) {
        pnkr::Log::error("Frame error: {}", e.what());
        break;
      }
    }

    pnkr::Log::info("Engine shutdown (rendered {} frames)", frame_count);
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "FATAL ERROR: " << e.what() << std::endl;
    return 1;
  }
}
