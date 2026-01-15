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
}
