#include "VulkanTestContext.hpp"

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include <doctest/doctest.h>
#include <filesystem>
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
} // namespace

TEST_CASE("Vulkan Graphics Pipeline Creation") {
  pnkr::tests::VulkanTestContext ctx;
  ctx.setup();
  auto *device = ctx.device();

  auto vertShader = Shader::load(
      ShaderStage::Vertex, resolveShaderPath("test_vertex.spv"), {});
  REQUIRE(vertShader != nullptr);

  auto fragShader = Shader::load(
      ShaderStage::Fragment, resolveShaderPath("test_fragment.spv"), {});
  REQUIRE(fragShader != nullptr);

  SUBCASE("Basic Pipeline") {
    auto desc = RHIPipelineBuilder()
                    .setShaders(vertShader.get(), fragShader.get())
                    .setTopology(PrimitiveTopology::TriangleList)
                    .setNoBlend()
                    .disableDepthTest()
                    .setColorFormat(Format::R8G8B8A8_UNORM)
                    .buildGraphics();

    auto pipeline = device->createGraphicsPipeline(desc);
    REQUIRE(pipeline != nullptr);
  }

  SUBCASE("Pipeline with Rasterization States") {
    auto desc = RHIPipelineBuilder()
                    .setShaders(vertShader.get(), fragShader.get())
                    .setTopology(PrimitiveTopology::TriangleList)
                    .setPolygonMode(PolygonMode::Line)
                    .setCullMode(CullMode::Back)
                    .setLineWidth(2.0f)
                    .disableDepthTest()
                    .setColorFormat(Format::R8G8B8A8_UNORM)
                    .buildGraphics();

    auto pipeline = device->createGraphicsPipeline(desc);
    REQUIRE(pipeline != nullptr);
  }

  SUBCASE("Multiple Pipelines") {
    std::vector<std::unique_ptr<RHIPipeline>> pipelines;
    pipelines.reserve(25);

    for (int i = 0; i < 25; ++i) {
      auto desc = RHIPipelineBuilder()
                      .setShaders(vertShader.get(), fragShader.get())
                      .setTopology(PrimitiveTopology::TriangleList)
                      .setColorFormat(Format::R8G8B8A8_UNORM)
                      .buildGraphics();
      auto pipe = device->createGraphicsPipeline(desc);
      if (pipe) {
        pipelines.push_back(std::move(pipe));
      }
    }

    CHECK(pipelines.size() == 25);
  }

  ctx.teardown();
}
