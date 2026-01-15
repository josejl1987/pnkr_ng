#include "VulkanTestContext.hpp"

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace pnkr::renderer::rhi;

namespace {
struct ComputeParams {
  uint32_t inputValue = 0;
  uint32_t outputCount = 0;
};

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
} // namespace

TEST_CASE("Vulkan Compute Pipeline") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  auto shaderPath = resolveShaderPath("test_compute.spv");
  auto shader = Shader::load(ShaderStage::Compute, shaderPath, {});
  REQUIRE(shader != nullptr);
  CHECK(shader->reflection().entryPoint == "computeMain");

  auto desc =
      RHIPipelineBuilder().setComputeShader(shader.get()).buildCompute();
  auto pipeline = device->createComputePipeline(desc);
  REQUIRE(pipeline != nullptr);

  ctx.teardown();
}

TEST_CASE("Vulkan Compute Dispatch") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  auto shaderPath = resolveShaderPath("test_compute.spv");
  auto shader = Shader::load(ShaderStage::Compute, shaderPath, {});
  REQUIRE(shader != nullptr);

  auto desc =
      RHIPipelineBuilder().setComputeShader(shader.get()).buildCompute();
  auto pipeline = device->createComputePipeline(desc);
  REQUIRE(pipeline != nullptr);

  auto *layout = pipeline->descriptorSetLayout(0);
  REQUIRE(layout != nullptr);
  auto set = device->allocateDescriptorSet(layout);
  REQUIRE(set != nullptr);

  constexpr uint32_t kOutputCount = 1024;

  BufferDescriptor inputDesc{};
  inputDesc.size = sizeof(ComputeParams);
  inputDesc.usage = BufferUsage::UniformBuffer;
  inputDesc.memoryUsage = MemoryUsage::CPUToGPU;
  auto inputBuffer = device->createBuffer("InputParams", inputDesc);
  REQUIRE(inputBuffer != nullptr);

  BufferDescriptor outputDesc{};
  outputDesc.size = kOutputCount * sizeof(uint32_t);
  outputDesc.usage = BufferUsage::StorageBuffer;
  outputDesc.memoryUsage = MemoryUsage::GPUToCPU;
  auto outputBuffer = device->createBuffer("OutputBuffer", outputDesc);
  REQUIRE(outputBuffer != nullptr);

  set->updateBuffer(0, inputBuffer.get(), 0, sizeof(ComputeParams));
  set->updateBuffer(1, outputBuffer.get(), 0, outputDesc.size);

  SUBCASE("Basic Compute Dispatch") {
    ComputeParams params{};
    params.inputValue = 42;
    params.outputCount = kOutputCount;
    std::memcpy(inputBuffer->map(), &params, sizeof(params));
    inputBuffer->unmap();

    device->immediateSubmit([&](RHICommandList *cmd) {
      cmd->bindPipeline(pipeline.get());
      cmd->bindDescriptorSet(0, set.get());
      cmd->dispatch((kOutputCount + 63) / 64, 1, 1);
    });
    device->waitIdle();

    std::vector<uint32_t> readback(kOutputCount);
    outputBuffer->invalidate(0, outputDesc.size);
    std::memcpy(readback.data(), outputBuffer->map(),
                outputDesc.size);
    outputBuffer->unmap();

    for (uint32_t i = 0; i < kOutputCount; ++i) {
      CHECK(readback[i] == 42 + i);
    }
  }

  SUBCASE("Multiple Sequential Dispatches") {
    for (uint32_t iteration = 0; iteration < 5; ++iteration) {
      ComputeParams params{};
      params.inputValue = iteration * 100;
      params.outputCount = kOutputCount;
      std::memcpy(inputBuffer->map(), &params, sizeof(params));
      inputBuffer->unmap();

      device->immediateSubmit([&](RHICommandList *cmd) {
        cmd->bindPipeline(pipeline.get());
        cmd->bindDescriptorSet(0, set.get());
        cmd->dispatch((kOutputCount + 63) / 64, 1, 1);
      });
      device->waitIdle();

      std::vector<uint32_t> readback(kOutputCount);
      outputBuffer->invalidate(0, outputDesc.size);
      std::memcpy(readback.data(), outputBuffer->map(),
                  outputDesc.size);
      outputBuffer->unmap();

      for (uint32_t i = 0; i < kOutputCount; ++i) {
        CHECK(readback[i] == iteration * 100 + i);
      }
    }
  }

  ctx.teardown();
}
