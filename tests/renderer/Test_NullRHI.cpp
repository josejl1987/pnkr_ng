#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include <doctest/doctest.h>

using namespace pnkr::renderer::rhi;

TEST_CASE("Null RHI Initialization") {
  DeviceDescriptor desc{};
  desc.enableBindless = true;

  auto devices = RHIFactory::enumeratePhysicalDevices(RHIBackend::Null);
  REQUIRE(devices.size() > 0);

  auto device =
      RHIFactory::createDevice(RHIBackend::Null, std::move(devices[0]), desc);
  REQUIRE(device != nullptr);

  SUBCASE("Create Buffer") {
    BufferDescriptor bufferDesc{};
    bufferDesc.size = 1024;
    bufferDesc.usage = BufferUsage::StorageBuffer;

    auto buffer = device->createBuffer("TestBuffer", bufferDesc);
    REQUIRE(buffer != nullptr);
    CHECK(buffer->size() == 1024);

    void *ptr = buffer->map();
    REQUIRE(ptr != nullptr);

    uint32_t testData = 0xDEADBEEF;
    std::memcpy(ptr, &testData, sizeof(testData));
    buffer->unmap();

    // Map again to verify persistence in Null RHI
    void *ptr2 = buffer->map();
    CHECK(*static_cast<uint32_t *>(ptr2) == 0xDEADBEEF);
    buffer->unmap();
  }

  SUBCASE("Create Texture") {
    TextureDescriptor texDesc{};
    texDesc.extent = {256, 256, 1};
    texDesc.format = Format::R8G8B8A8_UNORM;
    texDesc.usage = TextureUsage::Sampled;

    auto texture = device->createTexture("TestTexture", texDesc);
    REQUIRE(texture != nullptr);
    CHECK(texture->extent().width == 256);
    CHECK(texture->format() == Format::R8G8B8A8_UNORM);
  }
}
