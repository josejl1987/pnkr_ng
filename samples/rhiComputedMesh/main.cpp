#include "pnkr/core/common.hpp"
#include "pnkr/engine.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/app/Application.hpp"
#include <deque>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <vector>

using namespace pnkr;

struct PerFrameData {
  glm::mat4 MVP;
  uint64_t bufferAddress;
  uint32_t textureId;
  float time;
  uint32_t numU;
  uint32_t numV;
  float minU, maxU;
  float minV, maxV;
  uint32_t P1, P2;
  uint32_t Q1, Q2;
  float morph;
};

float easing(float x) {
  return (x < 0.5f)
             ? (4.0f * x * x * (3.0f * x - 1.0f))
             : (4.0f * (x - 1.0f) * (x - 1.0f) * (3.0f * (x - 1.0f) + 1.0f) +
                1.0f);
}

constexpr uint32_t kNumU = 512;
constexpr uint32_t kNumV = 512;

class ComputedMeshApp : public app::Application {
public:
  ComputedMeshApp()
      : app::Application({.title = "RHI Computed Mesh",
                          .width = 1280,
                          .height = 720,
                          .createRenderer = false}) {}

  renderer::scene::Camera m_camera;

  BufferHandle m_vertexBuffer;
  BufferHandle m_indexBuffer;
  std::unique_ptr<renderer::rhi::RHITexture> m_textureResource;
  uint32_t m_textureBindlessIndex;

  PipelineHandle m_compMeshPipeline;
  PipelineHandle m_compTexPipeline;
  PipelineHandle m_gfxPipeline;

  std::unique_ptr<renderer::rhi::RHIDescriptorSet> m_texComputeSet;

  std::deque<std::pair<uint32_t, uint32_t>> m_morphQueue = {{5, 8}, {5, 8}};
  float m_morphCoef = 0.0f;
  float m_animationSpeed = 1.0f;
  bool m_useColoredMesh = false;
  uint32_t m_indexCount = 0;
  bool m_firstFrame = true;

  void onInit() override {
    renderer::RendererConfig config;
    config.m_useBindless = true;
    m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

    m_camera.setPerspective(glm::radians(45.0f),
                            (float)m_config.width / m_config.height, 0.1f,
                            1000.0f);
    m_camera.lookAt({0.0f, 0.0f, 18.0f}, {0.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f});

    initResources();
    initPipelines();
    initDescriptors();
    initUI();

    m_renderer->setComputeRecordFunc(
        [this](const renderer::RHIFrameContext &ctx) { recordCompute(ctx); });
  }

  void initResources() {
    std::vector<uint32_t> indices;
    indices.resize((kNumU - 1) * (kNumV - 1) * 6);
    for (uint32_t j = 0; j < kNumV - 1; j++) {
      for (uint32_t i = 0; i < kNumU - 1; i++) {
        uint32_t ofs = (j * (kNumU - 1) + i) * 6;
        uint32_t i1 = (j + 0) * kNumU + (i + 0);
        uint32_t i2 = (j + 0) * kNumU + (i + 1);
        uint32_t i3 = (j + 1) * kNumU + (i + 1);
        uint32_t i4 = (j + 1) * kNumU + (i + 0);
        indices[ofs + 0] = i1;
        indices[ofs + 1] = i2;
        indices[ofs + 2] = i4;
        indices[ofs + 3] = i2;
        indices[ofs + 4] = i3;
        indices[ofs + 5] = i4;
      }
    }
    m_indexCount = (uint32_t)indices.size();
    m_indexBuffer = m_renderer->createBuffer(
        {.size = indices.size() * sizeof(uint32_t),
         .usage = renderer::rhi::BufferUsage::IndexBuffer |
                  renderer::rhi::BufferUsage::TransferDst,
         .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
         .data = indices.data(),
         .debugName = "ComputedMesh_Indices"});

    uint64_t vSize = 12 * sizeof(float) * kNumU * kNumV;
    m_vertexBuffer = m_renderer->createBuffer(
        {.size = vSize,
         .usage = renderer::rhi::BufferUsage::VertexBuffer |
                  renderer::rhi::BufferUsage::StorageBuffer |
                  renderer::rhi::BufferUsage::ShaderDeviceAddress,
         .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
         .debugName = "ComputedMesh_Vertices"});

    m_textureResource = m_renderer->device()->createTexture(
        renderer::rhi::Extent3D{1024, 1024, 1},
        renderer::rhi::Format::R8G8B8A8_UNORM,
        renderer::rhi::TextureUsage::Storage |
            renderer::rhi::TextureUsage::Sampled,
        1, 1);
    auto *bindless = m_renderer->device()->getBindlessManager();
    auto bindlessHandle = bindless->registerTexture(
        m_textureResource.get(),
        m_renderer->device()
            ->createSampler(renderer::rhi::Filter::Linear,
                            renderer::rhi::Filter::Linear,
                            renderer::rhi::SamplerAddressMode::Repeat)
            .get());
    m_textureBindlessIndex = util::u32(bindlessHandle);
  }

  void initPipelines() {
    renderer::rhi::ReflectionConfig reflectConfig;

    auto csTex = renderer::rhi::Shader::load(
        renderer::rhi::ShaderStage::Compute, getShaderPath("texture.comp.spv"),
        reflectConfig);
    m_compTexPipeline =
        m_renderer->createComputePipeline(renderer::rhi::RHIPipelineBuilder()
                                              .setComputeShader(csTex.get())
                                              .setName("CompTexture")
                                              .buildCompute());

    auto csMesh = renderer::rhi::Shader::load(
        renderer::rhi::ShaderStage::Compute, getShaderPath("mesh.comp.spv"),
        reflectConfig);
    m_compMeshPipeline =
        m_renderer->createComputePipeline(renderer::rhi::RHIPipelineBuilder()
                                              .setComputeShader(csMesh.get())
                                              .setName("CompMesh")
                                              .buildCompute());

    auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex,
                                          getShaderPath("mesh.vert.spv"),
                                          reflectConfig);
    auto gs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Geometry,
                                          getShaderPath("mesh.geom.spv"),
                                          reflectConfig);
    auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment,
                                          getShaderPath("mesh.frag.spv"),
                                          reflectConfig);

    renderer::rhi::GraphicsPipelineDescriptor desc =
        renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), gs.get())
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("GfxMesh")
            .buildGraphics();

    desc.vertexBindings = {
        {0, 12 * sizeof(float), renderer::rhi::VertexInputRate::Vertex}};
    desc.vertexAttributes = {{0, 0, renderer::rhi::Format::R32G32B32A32_SFLOAT,
                              0, renderer::rhi::VertexSemantic::Position},
                             {1, 0, renderer::rhi::Format::R32G32B32A32_SFLOAT,
                              16, renderer::rhi::VertexSemantic::TexCoord},
                             {2, 0, renderer::rhi::Format::R32G32B32A32_SFLOAT,
                              32, renderer::rhi::VertexSemantic::Normal}};
    m_gfxPipeline = m_renderer->createGraphicsPipeline(desc);
  }

  void initDescriptors() {
    auto *layout =
        m_renderer->pipeline(m_compTexPipeline)->descriptorSetLayout(0);
    m_texComputeSet = m_renderer->device()->allocateDescriptorSet(layout);
    m_texComputeSet->updateTexture(0, m_textureResource.get(), nullptr);
  }

  PerFrameData getPerFrameData(float time) {
    PerFrameData pc{};
    pc.time = time;
    pc.numU = kNumU;
    pc.numV = kNumV;
    pc.minU = -1.0f;
    pc.maxU = 1.0f;
    pc.minV = -1.0f;
    pc.maxV = 1.0f;
    auto iter = m_morphQueue.begin();
    pc.P1 = iter->first;
    pc.Q1 = iter->second;
    pc.P2 = (iter + 1)->first;
    pc.Q2 = (iter + 1)->second;
    pc.morph = easing(m_morphCoef);
    pc.bufferAddress =
        m_renderer->getBuffer(m_vertexBuffer)->getDeviceAddress();
    pc.textureId = m_useColoredMesh ? 0xFFFFFFFF : m_textureBindlessIndex;

    float aspect = (float)m_window.width() / m_window.height();
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
    proj[1][1] *= -1;
    pc.MVP = proj * m_camera.view() *
             glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));
    return pc;
  }

  void onUpdate(float dt) override {
    if (m_morphQueue.size() >= 2) {
      m_morphCoef += dt * m_animationSpeed;
      if (m_morphCoef >= 1.0f) {
        m_morphCoef = 0.0f;
        m_morphQueue.pop_front();
        if (m_morphQueue.size() < 2)
          m_morphQueue.push_back(m_morphQueue.back());
      }
    }
  }

  void onImGui() override {
    ImGui::Begin("Torus Knot Params");
    ImGui::Checkbox("Use colored mesh", &m_useColoredMesh);
    ImGui::SliderFloat("Speed", &m_animationSpeed, 0.0f, 2.0f);
    static const std::vector<std::pair<uint32_t, uint32_t>> PQ = {
        {1, 1}, {2, 3}, {2, 5}, {2, 7}, {3, 4}, {2, 9}, {3, 5}, {5, 8}, {8, 9}};
    for (const auto &pair : PQ) {
      const std::string label =
          std::to_string(pair.first) + ", " + std::to_string(pair.second);
      if (ImGui::Button(label.c_str(), ImVec2(60, 0))) {
        if (pair != m_morphQueue.back())
          m_morphQueue.push_back(pair);
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Text("Queue:");
    for (size_t i = 0; i < m_morphQueue.size(); ++i) {
      ImGui::Text("P=%u, Q=%u %s", m_morphQueue[i].first,
                  m_morphQueue[i].second, (i == 0) ? "<--" : "");
    }
    ImGui::End();
  }

  void recordCompute(const renderer::RHIFrameContext &ctx) {
    auto *cmd = ctx.commandBuffer;
    PerFrameData pc = getPerFrameData((float)ctx.frameIndex * 0.016f);

    if (!m_useColoredMesh) {
      renderer::rhi::RHIMemoryBarrier imgBarrier{};
      imgBarrier.texture = m_textureResource.get();
      imgBarrier.srcAccessStage = m_firstFrame
                                      ? renderer::rhi::ShaderStage::None
                                      : renderer::rhi::ShaderStage::Fragment;
      imgBarrier.dstAccessStage = renderer::rhi::ShaderStage::Compute;
      imgBarrier.oldLayout =
          m_firstFrame ? renderer::rhi::ResourceLayout::Undefined
                       : renderer::rhi::ResourceLayout::ShaderReadOnly;
      imgBarrier.newLayout = renderer::rhi::ResourceLayout::General;
      cmd->pipelineBarrier(imgBarrier.srcAccessStage,
                           renderer::rhi::ShaderStage::Compute, {imgBarrier});

      cmd->bindPipeline(m_renderer->getPipeline(m_compTexPipeline));
      cmd->bindDescriptorSet(0, m_texComputeSet.get());
      cmd->pushConstants(renderer::rhi::ShaderStage::Compute, pc);
      cmd->dispatch(1024 / 16, 1024 / 16, 1);

      imgBarrier.srcAccessStage = renderer::rhi::ShaderStage::Compute;
      imgBarrier.dstAccessStage = renderer::rhi::ShaderStage::Fragment;
      imgBarrier.oldLayout = renderer::rhi::ResourceLayout::General;
      imgBarrier.newLayout = renderer::rhi::ResourceLayout::ShaderReadOnly;
      cmd->pipelineBarrier(renderer::rhi::ShaderStage::Compute,
                           renderer::rhi::ShaderStage::Fragment, {imgBarrier});
    }

    cmd->bindPipeline(m_renderer->getPipeline(m_compMeshPipeline));
    cmd->pushConstants(renderer::rhi::ShaderStage::Compute, pc);
    cmd->dispatch((kNumU * kNumV) / 16, 1, 1);

    renderer::rhi::RHIMemoryBarrier bufBarrier{};
    bufBarrier.buffer = m_renderer->getBuffer(m_vertexBuffer);
    bufBarrier.srcAccessStage = renderer::rhi::ShaderStage::Compute;
    bufBarrier.dstAccessStage = renderer::rhi::ShaderStage::Vertex;
    cmd->pipelineBarrier(renderer::rhi::ShaderStage::Compute,
                         renderer::rhi::ShaderStage::Vertex, {bufBarrier});

    m_firstFrame = false;
  }

  void onRecord(const renderer::RHIFrameContext &ctx) override {
    auto *cmd = ctx.commandBuffer;
    PerFrameData pc = getPerFrameData((float)ctx.frameIndex * 0.016f);

    cmd->bindPipeline(m_renderer->getPipeline(m_gfxPipeline));
    cmd->bindVertexBuffer(0, m_renderer->getBuffer(m_vertexBuffer), 0);
    cmd->bindIndexBuffer(m_renderer->getBuffer(m_indexBuffer), 0, false);
    renderer::rhi::RHIDescriptorSet *bindlessSet =
        m_renderer->device()->getBindlessDescriptorSet();
    cmd->bindDescriptorSet(1, bindlessSet);
    cmd->pushConstants(renderer::rhi::ShaderStage::Vertex |
                           renderer::rhi::ShaderStage::Fragment,
                       pc);
    cmd->drawIndexed(m_indexCount, 1, 0, 0, 0);
  }

  void onEvent(const SDL_Event &event) override {
    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
      m_renderer->resize(event.window.data1, event.window.data2);
    }
  }
};

int main(int argc, char **argv) {
  ComputedMeshApp app;
  return app.run();
}
