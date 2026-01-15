#include "VulkanTestContext.hpp"

#include <doctest/doctest.h>

using namespace pnkr::renderer::rhi;

TEST_CASE("Vulkan Fence Synchronization") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  SUBCASE("Fence Signal and Wait") {
    auto fence = device->createFence(false);
    REQUIRE(fence != nullptr);

    auto cmd = device->createCommandList();
    cmd->begin();
    cmd->end();

    device->submitCommands(cmd.get(), fence.get());
    CHECK(fence->wait());
    CHECK(fence->isSignaled());
  }

  SUBCASE("Signaled Fence Creation") {
    auto fence = device->createFence(true);
    REQUIRE(fence != nullptr);
    CHECK(fence->isSignaled());
  }

  SUBCASE("Timeline Semaphore Waits") {
    auto cmd = device->createCommandList();
    cmd->begin();
    cmd->end();

    device->submitCommands(cmd.get(), nullptr, {}, {1});
    device->waitForFences({1});
    device->waitForFrame(1);
    CHECK(device->getCompletedFrame() >= 1);
  }

  ctx.teardown();
}
