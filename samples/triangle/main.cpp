/**
 * @file main.cpp
 * @brief Triangle sample - Basic window and event loop
 *
 * Stage 0: No rendering yet, just event handling
 * Stage 1: Vulkan triangle rendering
 */

#include <pnkr/engine.hpp>
#include <iostream>

#include "pnkr/renderer/vulkan/vulkan_context.hpp"

int main() {



  try {
    pnkr::Log::init("[%H:%M:%S] [%-8l] %v");
    pnkr::Log::info("PNKR Engine v{}.{}.{}",
      PNKR_VERSION_MAJOR, PNKR_VERSION_MINOR, PNKR_VERSION_PATCH
    );

    pnkr::Window window("PNKR - Triangle", 800, 600);
    pnkr::Log::info("Window created: {}x{}", window.width(), window.height());

    pnkr::renderer::Renderer renderer(window);



    int frame_count = 0;
    while (window.isRunning()) {
      window.processEvents();

      renderer.beginFrame();
      renderer.drawFrame();
      renderer.endFrame();

      if (++frame_count % 60 == 0) {
        pnkr::Log::debug("Running... (frames: {})", frame_count);
      }

      // Stage 1: Rendering code will go here
      // vk::raii::Queue queue;
      // queue.present(...);
    }

    pnkr::Log::info("Engine shutdown (rendered {} frames)", frame_count);
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "FATAL ERROR: " << e.what() << std::endl;
    return 1;
  }
}
