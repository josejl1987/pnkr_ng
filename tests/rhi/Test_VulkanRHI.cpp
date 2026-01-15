#include "VulkanTestContext.hpp"

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <thread>
#include <vector>

using namespace pnkr::renderer::rhi;

namespace {
std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}
} // namespace

TEST_CASE("Vulkan RHI Initialization with Lavapipe") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();

  REQUIRE(ctx.device() != nullptr);
  auto &caps = ctx.device()->physicalDevice().capabilities();

  CHECK_FALSE(caps.discreteGPU);
  CHECK(toLower(caps.deviceName).find("llvmpipe") != std::string::npos);
  CHECK(caps.bindlessTextures);
  CHECK(caps.drawIndirectCount);

  ctx.teardown();
}

TEST_CASE("Vulkan Buffer Operations") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  SUBCASE("Create Buffer") {
    BufferDescriptor desc{};
    desc.size = 4096;
    desc.usage = BufferUsage::StorageBuffer;
    desc.memoryUsage = MemoryUsage::CPUToGPU;

    auto buffer = device->createBuffer("TestBuffer", desc);
    REQUIRE(buffer != nullptr);
    CHECK(buffer->size() == 4096);
  }

  SUBCASE("Create All Buffer Types") {
    std::array<BufferUsage, 7> usages = {
        BufferUsage::TransferSrc, BufferUsage::TransferDst,
        BufferUsage::UniformBuffer, BufferUsage::StorageBuffer,
        BufferUsage::IndexBuffer, BufferUsage::VertexBuffer,
        BufferUsage::IndirectBuffer};

    for (auto usage : usages) {
      BufferDescriptor desc{};
      desc.size = 256;
      desc.usage = usage;
      desc.memoryUsage = MemoryUsage::CPUToGPU;
      auto buffer = device->createBuffer("Buffer", desc);
      CHECK(buffer != nullptr);
      CHECK(buffer->size() == 256);
    }
  }

  SUBCASE("Map/Unmap Memory") {
    BufferDescriptor desc{};
    desc.size = 1024;
    desc.usage = BufferUsage::UniformBuffer;
    desc.memoryUsage = MemoryUsage::CPUToGPU;

    auto buffer = device->createBuffer("MapTest", desc);
    auto *ptr = buffer->map();
    REQUIRE(ptr != nullptr);

    uint32_t testData[] = {0xDEADBEEF, 0xCAFEBABE, 0xDEADC0DE};
    std::memcpy(ptr, testData, sizeof(testData));
    buffer->unmap();

    auto *ptr2 = buffer->map();
    CHECK(std::memcmp(ptr2, testData, sizeof(testData)) == 0);
    buffer->unmap();
  }

  SUBCASE("Buffer with Device Address") {
    BufferDescriptor desc{};
    desc.size = 2048;
    desc.usage = BufferUsage::StorageBuffer | BufferUsage::ShaderDeviceAddress;
    desc.memoryUsage = MemoryUsage::CPUToGPU;

    auto buffer = device->createBuffer("AddressBuffer", desc);
    REQUIRE(buffer != nullptr);
    CHECK(buffer->getDeviceAddress() != 0);
  }

  SUBCASE("Concurrent Buffer Creation") {
    constexpr int kThreads = 8;
    constexpr int kIterations = 64;
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIterations; ++i) {
          BufferDescriptor desc{};
          desc.size = 512;
          desc.usage = BufferUsage::StorageBuffer;
          desc.memoryUsage = MemoryUsage::CPUToGPU;
          auto buffer = device->createBuffer("ThreadBuffer", desc);
          if (!buffer) {
            ok = false;
          }
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    CHECK(ok);
  }

  SUBCASE("Concurrent Buffer Map/Unmap") {
    constexpr int kThreads = 8;
    constexpr int kIterations = 128;
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t]() {
        for (int i = 0; i < kIterations; ++i) {
          BufferDescriptor desc{};
          desc.size = 64;
          desc.usage = BufferUsage::UniformBuffer;
          desc.memoryUsage = MemoryUsage::CPUToGPU;
          auto buffer = device->createBuffer("MapThreadBuffer", desc);
          if (!buffer) {
            ok = false;
            continue;
          }

          auto *ptr = buffer->map();
          if (!ptr) {
            ok = false;
            continue;
          }
          uint32_t value = static_cast<uint32_t>(t * 1000 + i);
          std::memcpy(ptr, &value, sizeof(value));
          buffer->unmap();
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    CHECK(ok);
  }

  ctx.teardown();
}

TEST_CASE("Vulkan Texture Operations") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  SUBCASE("Create 2D Texture") {
    TextureDescriptor desc{};
    desc.extent = {512, 512, 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;

    auto texture = device->createTexture("TestTexture", desc);
    REQUIRE(texture != nullptr);
    CHECK(texture->extent().width == 512);
    CHECK(texture->extent().height == 512);
    CHECK(texture->format() == Format::R8G8B8A8_UNORM);
  }

  SUBCASE("Create All Texture Formats") {
    std::array<Format, 6> formats = {
        Format::R8G8B8A8_UNORM, Format::R32_SFLOAT,
        Format::R32G32B32A32_SFLOAT, Format::R16G16B16A16_UNORM,
        Format::D32_SFLOAT, Format::R32_UINT};

    for (auto format : formats) {
      TextureDescriptor desc{};
      desc.extent = {64, 64, 1};
      desc.format = format;
      desc.usage = TextureUsage::Sampled;
      auto texture = device->createTexture("FormatTest", desc);
      CHECK(texture != nullptr);
      CHECK(texture->format() == format);
    }
  }

  SUBCASE("Create Cubemap") {
    TextureDescriptor desc{};
    desc.type = TextureType::TextureCube;
    desc.extent = {256, 256, 1};
    desc.format = Format::R32G32B32A32_SFLOAT;
    desc.usage = TextureUsage::Sampled;
    desc.arrayLayers = 6;

    auto cubemap = device->createTexture("TestCubemap", desc);
    REQUIRE(cubemap != nullptr);
  }

  SUBCASE("Texture Upload and Readback") {
    TextureDescriptor desc{};
    desc.extent = {64, 64, 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst |
                 TextureUsage::TransferSrc;
    desc.memoryUsage = MemoryUsage::GPUOnly;

    auto texture = device->createTexture("UploadTest", desc);
    REQUIRE(texture != nullptr);

    const size_t size = 64 * 64 * 4;
    std::vector<std::byte> uploadData(size);
    for (size_t i = 0; i < size; i += 4) {
      uploadData[i] = std::byte{0xAB};
      uploadData[i + 1] = std::byte{0xCD};
      uploadData[i + 2] = std::byte{0xEF};
      uploadData[i + 3] = std::byte{0xFF};
    }

    texture->uploadData(uploadData);
    device->waitIdle();

    std::vector<std::byte> readback(size);
    device->downloadTexture(texture.get(), readback);

    CHECK(std::equal(readback.begin(), readback.end(), uploadData.begin()));
  }

  SUBCASE("Mipmapped Texture") {
    TextureDescriptor desc{};
    desc.extent = {128, 128, 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    desc.mipLevels = 7;

    auto texture = device->createTexture("MipmapTest", desc);
    REQUIRE(texture != nullptr);
    CHECK(texture->mipLevels() == desc.mipLevels);
  }

  SUBCASE("Texture View Creation") {
    TextureDescriptor desc{};
    desc.extent = {256, 256, 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled;

    auto texture = device->createTexture("Base", desc);
    REQUIRE(texture != nullptr);

    TextureViewDescriptor viewDesc{};
    viewDesc.format = Format::R8G8B8A8_UNORM;
    auto view = device->createTextureView("View", texture.get(), viewDesc);
    REQUIRE(view != nullptr);
  }

  SUBCASE("Concurrent Texture Creation") {
    constexpr int kThreads = 4;
    constexpr int kIterations = 32;
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kIterations; ++i) {
          TextureDescriptor desc{};
          desc.extent = {32, 32, 1};
          desc.format = Format::R8G8B8A8_UNORM;
          desc.usage = TextureUsage::Sampled;
          auto texture = device->createTexture("ThreadTexture", desc);
          if (!texture) {
            ok = false;
          }
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    CHECK(ok);
  }

  ctx.teardown();
}
