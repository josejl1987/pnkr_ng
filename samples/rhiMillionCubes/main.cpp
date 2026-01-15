#include "pnkr/core/common.hpp"
#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/app/Application.hpp"
#include <glm/ext.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <variant>

using namespace pnkr;

struct CubePerFrameData {
  glm::mat4 viewproj;
  uint32_t textureId;
  uint64_t bufId;
  float time;
};

class PnkrMillionCubes : public app::Application {
public:
  PnkrMillionCubes()
      : app::Application({.title = "Pnkr Million cubes",
                          .width = 1280,
                          .height = 720,
                          .windowFlags = SDL_WINDOW_RESIZABLE,
                          .createRenderer = false}) {}

  renderer::scene::Camera m_camera;
  PipelineHandle m_pipeline;
  BufferHandle m_instanceBuffer;
  TextureHandle m_xorTexture;

  void onInit() override {
    renderer::RendererConfig config;
    config.m_useBindless = true;
    m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

    constexpr uint32_t texWidth = 256;
    constexpr uint32_t texHeight = 256;
    std::vector<uint32_t> pixels(texWidth * texHeight);
    for (uint32_t y = 0; y != texHeight; y++) {
      for (uint32_t x = 0; x != texWidth; x++) {
        pixels[y * texWidth + x] =
            0xFF000000 + ((x ^ y) << 16) + ((x ^ y) << 8) + (x ^ y);
      }
    }

    m_xorTexture = m_renderer->assets()->createTexture(
        reinterpret_cast<const unsigned char *>(pixels.data()), texWidth,
        texHeight, 4, true);

    const uint32_t kNumCubes = 1024 * 1024;
    std::vector<glm::vec4> centers(kNumCubes);
    for (glm::vec4 &p : centers) {
      p = glm::vec4(glm::linearRand(glm::vec3(-500.0f), glm::vec3(500.0f)),
                    glm::linearRand(0.0f, 3.14159f));
    }

    m_instanceBuffer = m_renderer->createBuffer(
        {.size = sizeof(glm::vec4) * kNumCubes,
         .usage = renderer::rhi::BufferUsage::StorageBuffer |
                  renderer::rhi::BufferUsage::ShaderDeviceAddress,
         .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
         .data = centers.data(),
         .debugName = "InstanceBuffer"});

    createPipeline();

    initUI();
  }

  void createPipeline() {

    renderer::rhi::ReflectionConfig config;

    auto vs = renderer::rhi::Shader::load(
        renderer::rhi::ShaderStage::Vertex,
        getShaderPath("million_cubes.vert.spv"), config);
    auto fs = renderer::rhi::Shader::load(
        renderer::rhi::ShaderStage::Fragment,
        getShaderPath("million_cubes.frag.spv"), config);
    auto builder =
        renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back, false)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("MillionCubes");

    m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
  }

  void onRecord(const renderer::RHIFrameContext &ctx) override {
    ctx.commandBuffer->bindPipeline(m_renderer->getPipeline(m_pipeline));

    renderer::rhi::RHIDescriptorSet *bindlessSet =
        m_renderer->device()->getBindlessDescriptorSet();
    ctx.commandBuffer->bindDescriptorSet(1, bindlessSet);

    float aspect = (float)m_window.width() / m_window.height();

    float time = ctx.deltaTime;
    static float accumulatedTime = 0.0f;
    accumulatedTime += ctx.deltaTime;

    glm::mat4 view = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 0.0f,
                  -1000.0f + 500.0f * (1.0f - cos(-accumulatedTime * 0.5f))));
    glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), aspect, 0.1f, 10000.0f);
    glm::mat4 viewProj = proj * view;

    CubePerFrameData data;
    data.viewproj = viewProj,
    data.textureId =
        util::u32(m_renderer->getTextureBindlessIndex(m_xorTexture)),
    data.bufId = m_renderer->getBuffer(m_instanceBuffer)->getDeviceAddress(),
    data.time = accumulatedTime;

    ctx.commandBuffer->pushConstants(renderer::rhi::ShaderStage::Vertex |
                                         renderer::rhi::ShaderStage::Fragment,
                                     data);

    const uint32_t kNumCubes = 1024 * 1024;
    ctx.commandBuffer->draw(36, kNumCubes);
  }

  void onEvent(const SDL_Event &event) override {
    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
      m_renderer->resize(event.window.data1, event.window.data2);
    }
  }

  void onShutdown() override {}
};

int main(int argc, char **argv) {
  PnkrMillionCubes app;
  return app.run();
}
