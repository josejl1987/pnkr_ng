#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

#include "../common/RhiSampleApp.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/random.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/transform.hpp>
#include <iostream>

#include <variant>
#include "generated/instanced_ducks.vert.h"

using namespace pnkr;

static glm::uvec2 packAddress(uint64_t address)
{
    return glm::uvec2(
        static_cast<uint32_t>(address & 0xFFFFFFFFu),
        static_cast<uint32_t>(address >> 32)
    );
}

static bool pickFirstPrimitive(
    const pnkr::renderer::scene::Model& model,
    MeshHandle& outMesh,
    TextureHandle& outTex)
{
    outMesh = INVALID_MESH_HANDLE;
    outTex = INVALID_TEXTURE_HANDLE;

    const auto& mats = model.materials();
    const auto& nodes = model.nodes();

    for (const auto& n : nodes)
    {
        for (const auto& prim : n.m_meshPrimitives)
        {
            outMesh = prim.m_mesh;

            if (prim.m_materialIndex < mats.size())
                outTex = mats[prim.m_materialIndex].m_baseColorTexture;

            return outMesh.isValid();
        }
    }
    return false;
}

class PnkrInstancedDucks : public samples::RhiSampleApp
{
public:
    PnkrInstancedDucks()
        : samples::RhiSampleApp({
            .title = "Pnkr Instanced Ducks", .width = 1824, .height = 928, .windowFlags = SDL_WINDOW_RESIZABLE,
            .createRenderer = false
        })
    {
    }

    renderer::scene::Camera m_camera;
    std::unique_ptr<renderer::RHIRenderer> m_renderer;
    PipelineHandle m_computePipeline;
    PipelineHandle m_graphicsPipeline;
    BufferHandle m_instanceBuffer;
    BufferHandle m_matrixBuffers[3]; // Ping-pong buffers
    MeshHandle m_duckMesh;
    TextureHandle m_duckTexture;

    bool m_hasHistory = false;
    float m_time = 0.0f;
    static constexpr uint32_t kNumInstances = 32 * 1024; // 32,000 ducks

    void onInit() override
    {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        loadDuckModel();
        createInstanceData();
        createMatrixBuffers();
        createPipelines();

        m_renderer->setComputeRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            recordCompute(ctx);
        });

        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx)
        {
            recordGraphics(ctx);
        });
    }

    void loadDuckModel()
    {
        // For now, we'll create a placeholder if no duck model is available
        // User should place their duck.glb file in the assets directory
        std::filesystem::path modelPath = "assets/rubber_duck/scene.gltf";

        if (!std::filesystem::exists(modelPath))
        {
            // Create a simple placeholder mesh (a box representing a duck)
            return;
        }

        // Load the actual duck model
        auto model = renderer::scene::Model::load(*m_renderer, modelPath, true);

        if (!model || !pickFirstPrimitive(*model, m_duckMesh, m_duckTexture))
        {
            std::cerr << "Failed to load duck model, creating placeholder" << std::endl;
            return;
        }

        // Create a placeholder texture if material had none
        if (!m_duckTexture.isValid())
        {
            createPlaceholderTexture();
        }
    }

    void createPlaceholderTexture()
    {
        // Create a simple yellow texture for the duck
        constexpr uint32_t texWidth = 64;
        constexpr uint32_t texHeight = 64;
        std::vector<uint32_t> pixels(texWidth * texHeight);

        for (uint32_t y = 0; y != texHeight; y++)
        {
            for (uint32_t x = 0; x != texWidth; x++)
            {
                // Yellow with some variation
                uint8_t intensity = 200 + (x ^ y) % 56;
                pixels[y * texWidth + x] = 0xFF000000 + (intensity << 16) + (intensity << 8) + (intensity >> 1);
            }
        }

        m_duckTexture = m_renderer->createTexture(reinterpret_cast<const unsigned char*>(pixels.data()), texWidth,
                                                  texHeight, 4, true);
    }

    void createInstanceData()
    {
        // Generate random positions and initial rotation angles for our ducks
        std::vector<glm::vec4> centers(kNumInstances);
        for (glm::vec4& p : centers)
        {
            p = glm::vec4(glm::linearRand(glm::vec3(-500.0f, -50.0f, -500.0f),
                                          glm::vec3(500.0f, 100.0f, 500.0f)),
                          glm::linearRand(0.0f, 3.14159f));
        }

        // Create instance buffer
        m_instanceBuffer = m_renderer->createBuffer({
            .size = sizeof(glm::vec4) * kNumInstances,
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress |
            renderer::rhi::BufferUsage::TransferDst,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
            .data = centers.data(),
            .debugName = "InstanceBuffer"
        });
    }

    void createMatrixBuffers()
    {
        // Create two alternating buffers for model matrices (ping-pong)
        for (int i = 0; i < 3; i++)
        {
            m_matrixBuffers[i] = m_renderer->createBuffer({
                .size = sizeof(glm::mat4) * kNumInstances,
                .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
                .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
                .debugName = std::string("MatrixBuffer" + std::to_string(i)).c_str()
            });
        }
    }

    void createPipelines()
    {
        createComputePipeline();
        createGraphicsPipeline();
    }

    void createComputePipeline()
    {
        // Configure reflection for our resources
        renderer::rhi::ReflectionConfig config;

        auto cs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Compute,
                                              getShaderPath("instanced_ducks_compute.comp.spv"), config);

        auto builder = renderer::rhi::RHIPipelineBuilder()
                       .setComputeShader(cs.get())
                       .setName("InstancedDucksCompute");

        m_computePipeline = m_renderer->createComputePipeline(builder.buildCompute());
    }

    void createGraphicsPipeline()
    {
        // Configure reflection for bindless resources
        renderer::rhi::ReflectionConfig config;

        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex,
                                              getShaderPath("instanced_ducks.vert.spv"), config);
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment,
                                              getShaderPath("instanced_ducks.frag.spv"), config);

        auto builder = renderer::rhi::RHIPipelineBuilder()
                       .setShaders(vs.get(), fs.get(), nullptr)
                       .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
                       .setCullMode(renderer::rhi::CullMode::Back)
                       .enableDepthTest()
                       .setColorFormat(m_renderer->getDrawColorFormat())
                       .setDepthFormat(m_renderer->getDrawDepthFormat())
                       .setName("InstancedDucksGraphics");

        m_graphicsPipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void onRenderFrame(float deltaTime) override
    {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }

    void recordCompute(const renderer::RHIFrameContext& ctx)
    {
        m_time += ctx.deltaTime;

        uint32_t writeBufferIndex = ctx.frameIndex % 2;
        dispatchComputeShader(ctx, m_time, writeBufferIndex);

        renderer::rhi::RHIMemoryBarrier barrier{};
        barrier.buffer = m_renderer->getBuffer(m_matrixBuffers[writeBufferIndex]);

        ctx.commandBuffer->pipelineBarrier(
            renderer::rhi::ShaderStage::Compute,
            renderer::rhi::ShaderStage::Vertex,
            {barrier}
        );
    }

    void recordGraphics(const renderer::RHIFrameContext& ctx)
    {
        float aspect = float(m_window.width()) / m_window.height();

        const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f,
                                                                         -1000.0f + 500.0f * (1.0f -
                                                                             cos(-m_time * 0.5f))));


        const glm::mat4 proj = glm::perspective(45.0f, aspect, 0.2f, 1500.0f);
        glm::mat4 viewProj = proj * view;

        uint32_t readBufferIndex = ctx.frameIndex % 2;
        renderInstancedDucks(ctx, viewProj, readBufferIndex);

    }

    void dispatchComputeShader(const renderer::RHIFrameContext& ctx,
                               float time, uint32_t matrixBufferIndex)
    {
        m_renderer->bindComputePipeline(ctx.commandBuffer, m_computePipeline);

        // Push constants for compute shader
        ShaderGen::PushData computeData{};
        computeData.time = time;
        computeData.instanceCount = kNumInstances;
        computeData.bufPosAngleIdPtr = m_renderer->getBuffer(m_instanceBuffer)->getDeviceAddress();
        computeData.matrixBufferPtr = m_renderer->getBuffer(m_matrixBuffers[matrixBufferIndex])->getDeviceAddress();

        m_renderer->pushConstants(ctx.commandBuffer, m_computePipeline,
                                  renderer::rhi::ShaderStage::Compute,
                                  computeData);

        // Dispatch compute shader: 32 work items per workgroup
        ctx.commandBuffer->dispatch((kNumInstances + 31) / 32, 1, 1);
    }

    void renderInstancedDucks(const renderer::RHIFrameContext& ctx, const glm::mat4& viewProj,
                              uint32_t matrixBufferIndex)
    {
        m_renderer->bindPipeline(ctx.commandBuffer, m_graphicsPipeline);

        // Bind global bindless descriptor set
        void* nativeSet = m_renderer->device()->getBindlessDescriptorSetNative();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_graphicsPipeline), 1, nativeSet);

        // Push constants for graphics shader
        ShaderGen::PushData graphicsData{};
        graphicsData.viewproj = viewProj;
        graphicsData.textureId = m_renderer->getTextureBindlessIndex(m_duckTexture);
        graphicsData.instanceCount = kNumInstances;
        graphicsData.bufPosAngleIdPtr = (m_renderer->getBuffer(m_instanceBuffer)->getDeviceAddress());
        graphicsData.matrixBufferPtr = (
            m_renderer->getBuffer(m_matrixBuffers[matrixBufferIndex])->getDeviceAddress());
        graphicsData.vertexBufferPtr = (m_renderer->getMeshVertexBufferAddress(m_duckMesh));

        m_renderer->pushConstants(ctx.commandBuffer, m_graphicsPipeline,
                                  renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment,
                                  graphicsData);

        // Bind mesh and draw instances
        m_renderer->bindMesh(ctx.commandBuffer, m_duckMesh);
        m_renderer->drawMeshInstanced(ctx.commandBuffer, m_duckMesh, kNumInstances);
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }

    void onShutdown() override
    {
    }
};

int main(int argc, char** argv)
{
    PnkrInstancedDucks app;
    return app.run();
}
