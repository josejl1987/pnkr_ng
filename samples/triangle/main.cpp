/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <filesystem>
#include <cstdint>
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

    pnkr::renderer::VulkanPipeline::Config triCfg{};
    triCfg.m_vertSpvPath = shaderDir / "triangle.vert.spv";
    triCfg.m_fragSpvPath = shaderDir / "triangle.frag.spv";
    triCfg.m_cullMode = vk::CullModeFlagBits::eNone;
    if (!fs::exists(triCfg.m_vertSpvPath)) {
      pnkr::Log::error("Vertex shader not found: {}",
                       triCfg.m_vertSpvPath.string());
      return 1;
    }
    if (!fs::exists(triCfg.m_fragSpvPath)) {
      pnkr::Log::error("Fragment shader not found: {}",
                       triCfg.m_fragSpvPath.string());
      return 1;
    }

    pnkr::renderer::Renderer renderer(window);
    const pnkr::renderer::PipelineHandle triPipe =
        renderer.createPipeline(triCfg);

    renderer.setRecordFunc([&](const pnkr::renderer::RenderFrameContext& ctx) {
      renderer.bindPipeline(ctx.m_cmd, triPipe);
      ctx.m_cmd.draw(3, 1, 0, 0);
    });

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    float deltaTime = 0.0f;

    int frameCount = 0;
    while (window.isRunning()) {
      try {
        window.processEvents();

        uint64_t now = SDL_GetPerformanceCounter();
        deltaTime = float(now - last) / float(freq);
        last = now;

        // avoid crazy jumps when resizing / breakpoints
        if (deltaTime > 0.05f) deltaTime = 0.05f;

        renderer.beginFrame(deltaTime);
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
