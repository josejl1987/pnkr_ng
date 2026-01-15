#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include <doctest/doctest.h>
#include <thread>
#include <vector>

using namespace pnkr::renderer;
using namespace pnkr::renderer::rhi;

TEST_CASE("RHIResourceManager Lifetime") {
    DeviceDescriptor desc{};
    desc.enableBindless = true;

    auto devices = RHIFactory::enumeratePhysicalDevices(RHIBackend::Null);
    auto device = RHIFactory::createDevice(RHIBackend::Null, std::move(devices[0]), desc);
    
    RHIResourceManager manager(device.get(), 3);

    SUBCASE("Deferred Destruction via SmartHandle") {
        TextureDescriptor texDesc{};
        texDesc.extent = {256, 256, 1};
        texDesc.format = Format::R8G8B8A8_UNORM;
        texDesc.usage = TextureUsage::Sampled;

        {
            TexturePtr tex = manager.createTexture("TestTexture", texDesc, true);
            CHECK(manager.getResourceStats().texturesAlive == 1);
        }

        CHECK(manager.getResourceStats().texturesAlive == 1);

        manager.processDestroyEvents();
        CHECK(manager.getResourceStats().texturesAlive == 0);
        CHECK(manager.getResourceStats().texturesDeferred == 1);

        manager.flushDeferred(0);
        CHECK(manager.getResourceStats().texturesDeferred == 0);
    }

    SUBCASE("Pipeline destruction") {
        GraphicsPipelineDescriptor pipeDesc{};
        PipelinePtr pipe = manager.createGraphicsPipeline(pipeDesc);
        CHECK(manager.getResourceStats().pipelinesAlive == 1);

        pipe.reset();
        CHECK(manager.getResourceStats().pipelinesAlive == 1);

        manager.processDestroyEvents();
        CHECK(manager.getResourceStats().pipelinesAlive == 0);
        CHECK(manager.getResourceStats().pipelinesDeferred == 1);

        manager.flushDeferred(0);
        CHECK(manager.getResourceStats().pipelinesDeferred == 0);
    }

    SUBCASE("Concurrent SmartHandle Churn") {
        TextureDescriptor texDesc{};
        texDesc.extent = {1, 1, 1};
        texDesc.format = Format::R8G8B8A8_UNORM;
        texDesc.usage = TextureUsage::Sampled;

        TexturePtr original = manager.createTexture("Base", texDesc, false);
        
        const int numThreads = 8;
        const int iterations = 10000;
        std::vector<std::thread> threads;

        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < iterations; ++j) {
                    TexturePtr copy = original;
                    volatile int x = (int)copy.handle().index;
                    (void)x;
                }
            });
        }

        for (auto& t : threads) t.join();

        auto* slot = manager.textures().getSlotPtr(original.handle().index);
        CHECK(slot->refCount.load() == 1);
        
        original.reset();
        manager.processDestroyEvents();
        CHECK(manager.getResourceStats().texturesAlive == 0);
    }
    SUBCASE("Deferred Destruction Timing (Regression Test)") {
        // This test verifies that resources destroyed in frame N are NOT destroyed immediately
        // in frame N, but are deferred until frame N + FramesInFlight (effectively).
        
        TextureDescriptor texDesc{};
        texDesc.extent = {1, 1, 1};
        texDesc.format = Format::R8G8B8A8_UNORM;
        texDesc.usage = TextureUsage::Sampled;

        {
            TexturePtr tex = manager.createTexture("TimingTest", texDesc, true);
        } // tex goes out of scope, destroyDeferred called.
        
        // At this point, the destroy event is in the queue, but not processed.
        CHECK(manager.getResourceStats().texturesAlive == 1);
        CHECK(manager.getResourceStats().texturesDeferred == 0);

        // Simulate Frame 0 start:
        // flush(0) should:
        // 1. flushDeferred(0) -> CLEARS old garbage (nothing atm)
        // 2. processDestroyEvents() -> Moves event to deferred queue for slot 0
        manager.flush(0);

        // Resources should be "Alive" but "Deferred"
        // Wait, 'Alive' counts the StablePool slots. destroyTexture() calls retire() which effectively removes it from 'Alive' count in the pool?
        // RHIResourceManager::destroyTexture calls retire() and freeSlot(),
        // so texturesAlive should be 0, and texturesDeferred should be 1.
        
        CHECK(manager.getResourceStats().texturesAlive == 0);
        CHECK(manager.getResourceStats().texturesDeferred == 1);

        // If the bug existed, flush(0) would have processed events THEN flushed deferred,
        // resulting in texturesDeferred == 0 immediately.
        CHECK(manager.getResourceStats().texturesDeferred == 1);

        // Simulate Frame 0 again (or Frame N + FramesInFlight)
        manager.flush(0);
        
        // NOW it should be gone.
        CHECK(manager.getResourceStats().texturesDeferred == 0);
    }
}
