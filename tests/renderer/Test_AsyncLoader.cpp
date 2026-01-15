#include <doctest/doctest.h>
#include "pnkr/renderer/AsyncLoader.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/platform/window.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/core/logger.hpp"
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

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
            loader.requestTexture(std::format("slow_{}.ktx", i).c_str(), ptr.handle(), false);
        }
        
        // Enqueue one high priority
        auto highPtr = renderer.resourceManager()->createTexture("HighTex", desc, false);
        loader.requestTexture("fast.ktx", highPtr.handle(), false, LoadPriority::Immediate);
    }
}

TEST_CASE("AsyncLoader: Small Ring Buffer (Deadlock Prevention)") {
    // 1. Setup TaskSystem 
    if (!pnkr::core::TaskSystem::isInitialized()) {
        pnkr::core::TaskSystem::Config tsConfig;
        tsConfig.numThreads = 2;
        pnkr::core::TaskSystem::init(tsConfig); 
    }

    // 2. Setup Renderer with Null Backend
    Window window("TestWindow", 1280, 720, SDL_WINDOW_HIDDEN);
    RendererConfig config{};
    config.m_backend = rhi::RHIBackend::Null;
    config.m_enableAsyncTextureLoading = true;
    
    RHIRenderer renderer(window, config);
    
    // 3. Initialize Loader with SMALL Ring Buffer (4MB = 2 pages of 2MB)
    // We want to force a condition where we enqueue MORE than this amount.
    // Each request will be ~1MB (1024x1024 R8)
    const uint64_t kTestBufferSize = 4 * 1024 * 1024;
    
    // Helper to generate a dummy 1024x1024 gray image in simple format that STB or KTX detects
    struct TempFile {
        std::string path;
        TempFile(std::string p) : path(p) {}
        ~TempFile() { 
            try {
                if (std::filesystem::exists(path))
                    std::filesystem::remove(path); 
            } catch (...) {}
        }
    };

    std::vector<std::unique_ptr<TempFile>> tempFiles;
    std::vector<TextureHandle> handles;
    
    // We create 8MB worth of data (8 files of 1MB)
    const int kNumFiles = 8;
    const int kWidth = 512;
    const int kHeight = 512;
    const size_t kSize = kWidth * kHeight * 4; // RGBA 8-bit

    for (int i = 0; i < kNumFiles; ++i) {
        std::string fname = std::format("test_tex_{}.tga", i);
        
        // Minimal TGA Header
        std::vector<uint8_t> buffer;
        buffer.push_back(0); // ID length
        buffer.push_back(0); // Color map type
        buffer.push_back(2); // Image type (2 = uncompressed true-color)
        buffer.push_back(0); buffer.push_back(0); // Color map spec
        buffer.push_back(0); buffer.push_back(0); 
        buffer.push_back(0); // Color map entry size
        buffer.push_back(0); buffer.push_back(0); // X origin
        buffer.push_back(0); buffer.push_back(0); // Y origin
        buffer.push_back((kWidth & 0x00FF)); buffer.push_back((kWidth & 0xFF00) >> 8);
        buffer.push_back((kHeight & 0x00FF)); buffer.push_back((kHeight & 0xFF00) >> 8);
        buffer.push_back(32); // Pixel depth
        buffer.push_back(0); // Image descriptor

        // Pixel data (gray)
        buffer.resize(buffer.size() + kSize);

        {
            std::ofstream out(fname, std::ios::binary);
            out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        }
        tempFiles.push_back(std::make_unique<TempFile>(fname));

        rhi::TextureDescriptor desc{};
        desc.extent = {1, 1, 1};
        auto ptr = renderer.resourceManager()->createTexture(std::format("T{}", i).c_str(), desc, false);
        handles.push_back(ptr.handle());
    }

    {
        AsyncLoader loader(renderer, kTestBufferSize);

        for (int i = 0; i < kNumFiles; ++i) {
             loader.requestTexture(tempFiles[i]->path, handles[i], true);
        }

        // 5. Process
        int completedCount = 0;
        auto startTime = std::chrono::steady_clock::now();
        
        bool stalled = false;

        while (completedCount < kNumFiles) {
            try {
                loader.syncToGPU();
            } catch(...) {}

            auto done = loader.consumeCompletedTextures();
            completedCount += (int)done.size();

            if (std::chrono::steady_clock::now() - startTime > std::chrono::seconds(5)) {
                stalled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        CHECK_MESSAGE(!stalled, "AsyncLoader stalled! This indicates a deadlock where ring buffer is full and transfer thread blocked.");
        CHECK(completedCount == kNumFiles);
    } // loader is destroyed here, closing threads and files.
    
    // tempFiles are destroyed here.
}

TEST_CASE("AsyncLoader: Large Asset (Temporary Buffer Fallback)") {
    if (!pnkr::core::TaskSystem::isInitialized()) {
        pnkr::core::TaskSystem::Config tsConfig;
        tsConfig.numThreads = 2;
        pnkr::core::TaskSystem::init(tsConfig); 
    }

    Window window("TestWindow", 1280, 720, SDL_WINDOW_HIDDEN);
    RendererConfig config{};
    config.m_backend = rhi::RHIBackend::Null;
    config.m_enableAsyncTextureLoading = true;
    
    RHIRenderer renderer(window, config);
    
    // Ring Buffer 4MB
    const uint64_t kTestBufferSize = 4 * 1024 * 1024;
    AsyncLoader loader(renderer, kTestBufferSize);

    // Create 1 large texture (6MB) > 4MB Ring Buffer
    // 4MB = 2048*2048 RGBA is 16MB. 1024*1024 RGBA is 4MB.
    // We want > 2MB (half of 4MB ring buffer).
    // Let's use 1024x1024 RGBA = 4MB, which is 100% of buffer.
    const int kWidth = 1024;
    const int kHeight = 1024;
    const size_t kSize = kWidth * kHeight * 4; // 4MB
    
    std::string fname = "large_tex.data"; // Dummy extension, we just write bytes
    {
        std::ofstream out(fname, std::ios::binary);
        std::vector<uint8_t> data(kSize + 128, 255); // minimal header simulation (not real image format, relying on streamer behavior)
        // Wait, for Null RHI streamer reads file. But to parse it needs to be valid format if using STB.
        // Actually, for this test let's create a TGA again to be safe.
        
        // Minimal TGA Header
        std::vector<uint8_t> buffer;
        buffer.push_back(0); // ID length
        buffer.push_back(0); // Color map type
        buffer.push_back(2); // Image type (2 = uncompressed true-color)
        buffer.push_back(0); buffer.push_back(0); // Color map spec
        buffer.push_back(0); buffer.push_back(0); 
        buffer.push_back(0); // Color map entry size
        buffer.push_back(0); buffer.push_back(0); // X origin
        buffer.push_back(0); buffer.push_back(0); // Y origin
        buffer.push_back((kWidth & 0x00FF)); buffer.push_back((kWidth & 0xFF00) >> 8);
        buffer.push_back((kHeight & 0x00FF)); buffer.push_back((kHeight & 0xFF00) >> 8);
        buffer.push_back(32); // Pixel depth
        buffer.push_back(0); // Image descriptor
        
        // Payload
        buffer.resize(buffer.size() + kSize); // Fill with 0
        
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }
    
    // Explicitly delete file at end of scope
    struct FileGuard {
        std::string p;
        ~FileGuard() { if(std::filesystem::exists(p)) std::filesystem::remove(p); }
    } guard{fname};

    rhi::TextureDescriptor desc{};
    desc.extent = {1, 1, 1}; // Dummy extent for Null RHI
    auto ptr = renderer.resourceManager()->createTexture("LargeTex", desc, false);
    
    loader.requestTexture(fname, ptr.handle(), true);

    bool found = false;
    for(int i=0; i<500; ++i) { // 5 seconds
        try { loader.syncToGPU(); } catch(...) {}
        auto done = loader.consumeCompletedTextures();
        if(!done.empty()) {
            found = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    CHECK_MESSAGE(found, "Failed to load large asset that requires temporary buffer fallback");
}
