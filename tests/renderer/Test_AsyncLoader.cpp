#include <doctest/doctest.h>
#include "pnkr/renderer/AsyncLoader.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/platform/window.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/core/logger.hpp"
#include <thread>
#include <chrono>

using namespace pnkr::renderer;
using namespace pnkr::platform;

TEST_CASE("AsyncLoader Comprehensive Tests (Null RHI)") {
    // 1. Setup TaskSystem (if not already init)
    if (!pnkr::core::TaskSystem::isInitialized()) {
        pnkr::core::TaskSystem::Config tsConfig;
        tsConfig.numThreads = 2;
        pnkr::core::TaskSystem::init(tsConfig); 
    }

    // 2. Setup Null RHI Renderer
    // We need a window, even if hidden.
    Window window("TestWindow", 1280, 720, SDL_WINDOW_HIDDEN);
    
    RendererConfig config{};
    config.m_backend = rhi::RHIBackend::Null;
    config.m_enableAsyncTextureLoading = true;
    
    RHIRenderer renderer(window, config);
    AsyncLoader loader(renderer);

    SUBCASE("Basic texture request and completion") {
        // Request a real texture handle from the manager
        rhi::TextureDescriptor desc{};
        desc.extent = {1, 1, 1};
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled;
        auto ptr = renderer.resourceManager()->createTexture("TestTexture", desc, false);
        TextureHandle handle = ptr.handle();
        
        // Request a non-existent texture (it will fail IO, but we can check failure path)
        loader.requestTexture("non_existent.ktx", handle, false, LoadPriority::Medium);
        
        // Wait a bit for IO task and transfer thread to process
        int attempts = 0;
        bool found = false;
        while (attempts < 100) {
            try {
                loader.syncToGPU();
            } catch (const std::exception& e) {
                FAIL("syncToGPU threw exception: " << e.what());
            } catch (...) {
                FAIL("syncToGPU threw unknown exception");
            }
            auto completed = loader.consumeCompletedTextures();
            for (auto h : completed) {
                if (h == handle) {
                    found = true;
                    break;
                }
            }
            if (found) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            attempts++;
        }
        
        CHECK(found);
        
        auto stats = loader.getStatistics();
        CHECK(stats.failedLoads == 1);
    }

    SUBCASE("High priority preemption") {
        rhi::TextureDescriptor desc{};
        desc.extent = {1, 1, 1};
        desc.format = rhi::Format::R8G8B8A8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled;

        // Enqueue many normal priority
        for (int i = 0; i < 10; ++i) {
            auto ptr = renderer.resourceManager()->createTexture("SlowTex", desc, false);
            loader.requestTexture(std::format("slow_{}.ktx", i), ptr.handle(), false);
        }
        
        // Enqueue one high priority
        auto highPtr = renderer.resourceManager()->createTexture("HighTex", desc, false);
        loader.requestTexture("fast.ktx", highPtr.handle(), false, LoadPriority::Immediate);
    }
}
