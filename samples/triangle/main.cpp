/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <filesystem>
#include <iostream>
#include <pnkr/engine.hpp>

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  constexpr int kWindowWidth = 800;
  constexpr int kWindowHeight = 600;
  constexpr int kLogFps = 60;

  try {
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

    pnkr::Window window("PNKR - Triangle", kWindowWidth, kWindowHeight,
                        SDL_WINDOW_RESIZABLE);
    pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

    pnkr::renderer::RendererConfig rendererConfig{};
    rendererConfig.m_pipeline.m_vertSpvPath = shaderDir / "triangle.vert.spv";
    rendererConfig.m_pipeline.m_fragSpvPath = shaderDir / "triangle.frag.spv";

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

    pnkr::renderer::Renderer renderer(window, rendererConfig);

    int frameCount = 0;
    while (window.isRunning()) {
      try {
        window.processEvents();

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
