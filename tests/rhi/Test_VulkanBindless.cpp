#include "VulkanTestContext.hpp"

#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include <atomic>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace pnkr::renderer::rhi;

namespace {
std::filesystem::path resolveShaderPath(const char *name) {
  std::vector<std::filesystem::path> candidates = {
      std::filesystem::path("bin/shaders") / name,
      std::filesystem::path("tests/bin/shaders") / name,
      std::filesystem::path("..") / "bin/shaders" / name,
      std::filesystem::path("..") / "tests/bin/shaders" / name};

  for (const auto &path : candidates) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }
  return std::filesystem::path(name);
}

struct BindlessParams {
  uint32_t outputCount = 0;
};
} // namespace

TEST_CASE("Vulkan Bindless Allocation") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  auto *bindless = device->getBindlessManager();
  REQUIRE(bindless != nullptr);

  SUBCASE("Bindless Buffer Handles Unique") {
    std::vector<std::unique_ptr<RHIBuffer>> buffers;
    std::vector<BufferBindlessHandle> handles;

    for (uint32_t i = 0; i < 64; ++i) {
      BufferDescriptor desc{};
      desc.size = 256;
      desc.usage = BufferUsage::StorageBuffer;
      desc.memoryUsage = MemoryUsage::CPUToGPU;
      auto buffer = device->createBuffer("BindlessBuffer", desc);
      REQUIRE(buffer != nullptr);

      auto handle = bindless->registerBuffer(buffer.get());
      CHECK(handle.isValid());
      handles.push_back(handle);
      buffers.push_back(std::move(buffer));
    }

    std::unordered_set<uint32_t> unique;
    for (const auto &handle : handles) {
      unique.insert(handle.index());
    }
    CHECK(unique.size() == handles.size());
  }

  SUBCASE("Concurrent Bindless Buffer Registration") {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 32;
    std::atomic<bool> ok{true};
    std::mutex mutex;
    std::vector<std::unique_ptr<RHIBuffer>> buffers;
    std::vector<BufferBindlessHandle> handles;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&]() {
        for (int i = 0; i < kPerThread; ++i) {
          BufferDescriptor desc{};
          desc.size = 128;
          desc.usage = BufferUsage::StorageBuffer;
          desc.memoryUsage = MemoryUsage::CPUToGPU;
          auto buffer = device->createBuffer("ThreadBindlessBuffer", desc);
          if (!buffer) {
            ok = false;
            continue;
          }

          auto handle = bindless->registerBuffer(buffer.get());
          if (!handle.isValid()) {
            ok = false;
          }

          std::scoped_lock lock(mutex);
          buffers.push_back(std::move(buffer));
          handles.push_back(handle);
        }
      });
    }

    for (auto &thread : threads) {
      thread.join();
    }

    CHECK(ok);

    std::unordered_set<uint32_t> unique;
    for (const auto &handle : handles) {
      unique.insert(handle.index());
    }
    CHECK(unique.size() == handles.size());
  }

  SUBCASE("Bindless Texture Handles Unique") {
    std::vector<std::unique_ptr<RHITexture>> textures;
    std::vector<TextureBindlessHandle> handles;

    for (uint32_t i = 0; i < 16; ++i) {
      TextureDescriptor desc{};
      desc.extent = {32, 32, 1};
      desc.format = Format::R8G8B8A8_UNORM;
      desc.usage = TextureUsage::Sampled;
      auto texture = device->createTexture("BindlessTexture", desc);
      REQUIRE(texture != nullptr);

      auto handle = bindless->registerTexture2D(texture.get());
      CHECK(handle.isValid());
      handles.push_back(handle);
      textures.push_back(std::move(texture));
    }

    std::unordered_set<uint32_t> unique;
    for (const auto &handle : handles) {
      unique.insert(handle.index());
    }
    CHECK(unique.size() == handles.size());
  }

  ctx.teardown();
}

TEST_CASE("Vulkan Bindless Compute Dispatch") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  auto *bindless = device->getBindlessManager();
  REQUIRE(bindless != nullptr);

  auto shaderPath = resolveShaderPath("test_bindless.spv");
  auto shader = Shader::load(ShaderStage::Compute, shaderPath, {});
  REQUIRE(shader != nullptr);

  DescriptorSetLayout set0Layout{};
  set0Layout.bindings.push_back({
      .binding = 0,
      .type = DescriptorType::UniformBuffer,
      .count = 1,
      .stages = ShaderStage::Compute,
      .name = "params",
      .flags = DescriptorBindingFlags::None,
  });
  set0Layout.bindings.push_back({
      .binding = 1,
      .type = DescriptorType::StorageBuffer,
      .count = 1,
      .stages = ShaderStage::Compute,
      .name = "outputBuffer",
      .flags = DescriptorBindingFlags::None,
  });
  set0Layout.bindings.push_back({
      .binding = 2,
      .type = DescriptorType::StorageBuffer,
      .count = 1,
      .stages = ShaderStage::Compute,
      .name = "indices",
      .flags = DescriptorBindingFlags::None,
  });

  auto *bindlessLayoutHandle = device->getBindlessDescriptorSetLayout();
  REQUIRE(bindlessLayoutHandle != nullptr);

  auto desc = RHIPipelineBuilder()
                  .setComputeShader(shader.get())
                  .setDescriptorSetLayouts(
                      {set0Layout, bindlessLayoutHandle->description()})
                  .buildCompute();
  auto pipeline = device->createComputePipeline(desc);
  REQUIRE(pipeline != nullptr);

  auto set0Handle = device->createDescriptorSetLayout(set0Layout);
  REQUIRE(set0Handle != nullptr);
  auto set0 = device->allocateDescriptorSet(set0Handle.get());
  REQUIRE(set0 != nullptr);

  constexpr uint32_t kBufferCount = 16;
  std::vector<std::unique_ptr<RHIBuffer>> buffers;
  std::vector<uint32_t> indices;
  buffers.reserve(kBufferCount);
  indices.reserve(kBufferCount);

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    BufferDescriptor descBuf{};
    descBuf.size = sizeof(uint32_t);
    descBuf.usage = BufferUsage::StorageBuffer;
    descBuf.memoryUsage = MemoryUsage::CPUToGPU;
    auto buffer = device->createBuffer("BindlessInput", descBuf);
    REQUIRE(buffer != nullptr);

    uint32_t value = 100 + i;
    std::memcpy(buffer->map(), &value, sizeof(value));
    buffer->unmap();

    auto handle = bindless->registerBuffer(buffer.get());
    REQUIRE(handle.isValid());
    indices.push_back(handle.index());
    buffers.push_back(std::move(buffer));
  }

  BufferDescriptor outputDesc{};
  outputDesc.size = kBufferCount * sizeof(uint32_t);
  outputDesc.usage = BufferUsage::StorageBuffer;
  outputDesc.memoryUsage = MemoryUsage::GPUToCPU;
  auto outputBuffer = device->createBuffer("BindlessOutput", outputDesc);
  REQUIRE(outputBuffer != nullptr);

  BufferDescriptor paramsDesc{};
  paramsDesc.size = sizeof(BindlessParams);
  paramsDesc.usage = BufferUsage::UniformBuffer;
  paramsDesc.memoryUsage = MemoryUsage::CPUToGPU;
  auto paramsBuffer = device->createBuffer("BindlessParams", paramsDesc);
  REQUIRE(paramsBuffer != nullptr);

  BufferDescriptor indicesDesc{};
  indicesDesc.size = kBufferCount * sizeof(uint32_t);
  indicesDesc.usage = BufferUsage::StorageBuffer;
  indicesDesc.memoryUsage = MemoryUsage::CPUToGPU;
  auto indicesBuffer = device->createBuffer("BindlessIndices", indicesDesc);
  REQUIRE(indicesBuffer != nullptr);
  std::memcpy(indicesBuffer->map(), indices.data(), indicesDesc.size);
  indicesBuffer->unmap();

  BindlessParams params{};
  params.outputCount = kBufferCount;
  std::memcpy(paramsBuffer->map(), &params, sizeof(params));
  paramsBuffer->unmap();

  set0->updateBuffer(0, paramsBuffer.get(), 0, sizeof(BindlessParams));
  set0->updateBuffer(1, outputBuffer.get(), 0, outputDesc.size);
  set0->updateBuffer(2, indicesBuffer.get(), 0, indicesDesc.size);

  device->immediateSubmit([&](RHICommandList *cmd) {
    cmd->bindPipeline(pipeline.get());
    cmd->bindDescriptorSet(0, set0.get());
    cmd->bindDescriptorSet(1, device->getBindlessDescriptorSet());
    cmd->dispatch((kBufferCount + 63) / 64, 1, 1);
  });
  device->waitIdle();

  std::vector<uint32_t> readback(kBufferCount);
  outputBuffer->invalidate(0, outputDesc.size);
  std::memcpy(readback.data(), outputBuffer->map(), outputDesc.size);
  outputBuffer->unmap();

  for (uint32_t i = 0; i < kBufferCount; ++i) {
    CHECK(readback[i] == 100 + i);
  }

  ctx.teardown();
}
