#include "pnkr/app/Application.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"

using namespace pnkr;

struct ComputePushConstants {
    uint32_t textureIndex;
    float time;
};

struct GraphicsPushConstants {
    uint32_t textureIndex;
    uint32_t samplerIndex;
    float time;
};

class ComputeTextureApp : public app::Application {
public:
    ComputeTextureApp() 
        : app::Application({.title="RHI Compute Texture", .width=1280, .height=720, .createRenderer=false}) {}

    std::unique_ptr<renderer::rhi::RHITexture> m_texture;
    std::unique_ptr<renderer::rhi::RHISampler> m_sampler;
    uint32_t m_storageIndex = 0;
    uint32_t m_sampledIndex = 0;
    uint32_t m_samplerIndex = 0;
    PipelineHandle m_computePipeline;
    PipelineHandle m_graphicsPipeline;
    float m_time = 0.0f;

    void onInit() override {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        // 1. Create Texture manually to set Storage usage
        auto textureDesc = renderer::rhi::TextureDescriptor{};
        textureDesc.extent = {1280, 720, 1};
        textureDesc.format = renderer::rhi::Format::R8G8B8A8_UNORM;
        textureDesc.usage = renderer::rhi::TextureUsage::Storage | renderer::rhi::TextureUsage::Sampled;
        textureDesc.debugName = "ProceduralTexture";
        m_texture = m_renderer->device()->createTexture(textureDesc.extent, textureDesc.format, textureDesc.usage);
        
        // 2. Register to Bindless
        m_sampler = m_renderer->device()->createSampler(
            renderer::rhi::Filter::Linear, 
            renderer::rhi::Filter::Linear, 
            renderer::rhi::SamplerAddressMode::ClampToEdge);
        
        m_samplerIndex = m_renderer->device()->registerBindlessSampler(m_sampler.get()).index;
        m_sampledIndex = m_renderer->device()->registerBindlessTexture2D(m_texture.get()).index;
        m_storageIndex = m_renderer->device()->registerBindlessStorageImage(m_texture.get()).index;

        // 3. Compute Pipeline
        {
            renderer::rhi::ReflectionConfig reflect;
            auto cs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Compute, getShaderPath("industrial.comp.spv"), reflect);
            auto builder = renderer::rhi::RHIPipelineBuilder()
                .setComputeShader(cs.get())
                .setName("IndustrialCompute");
            m_computePipeline = m_renderer->createComputePipeline(builder.buildCompute());
        }

        // 4. Graphics Pipeline
        {
            renderer::rhi::ReflectionConfig reflect;
            auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("fullscreen.vert.spv"), reflect);
            auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("fullscreen.frag.spv"), reflect);
            
            auto builder = renderer::rhi::RHIPipelineBuilder()
                .setShaders(vs.get(), fs.get())
                .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
                .setCullMode(renderer::rhi::CullMode::None)
                .setDepthFormat(m_renderer->getDrawDepthFormat()) 
                .setColorFormat(m_renderer->getSwapchainColorFormat())
                .setName("FullscreenGraphics");
            
            m_graphicsPipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
        }
        
        m_renderer->setComputeRecordFunc([this](const renderer::RHIFrameContext& ctx){ recordCompute(ctx); });
        m_renderer->setRecordFunc([this](const renderer::RHIFrameContext& ctx){ recordGraphics(ctx); });
    }

    void recordCompute(const renderer::RHIFrameContext& ctx) {
        m_time += ctx.deltaTime;
        auto cmd = ctx.commandBuffer;

        // Barrier: Transition to General for Compute Write
        {
            renderer::rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_texture.get();
            barrier.srcAccessStage = renderer::rhi::ShaderStage::Fragment; 
            barrier.dstAccessStage = renderer::rhi::ShaderStage::Compute;
            barrier.oldLayout = renderer::rhi::ResourceLayout::Undefined; 
            barrier.newLayout = renderer::rhi::ResourceLayout::General;
            cmd->pipelineBarrier(renderer::rhi::ShaderStage::Fragment, renderer::rhi::ShaderStage::Compute, {barrier});
        }

        m_renderer->bindComputePipeline(cmd, m_computePipeline);
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        cmd->bindDescriptorSet(m_renderer->pipeline(m_computePipeline), 1, bindlessSet);

        ComputePushConstants pc { m_storageIndex, m_time };
        m_renderer->pushConstants(cmd, m_computePipeline, renderer::rhi::ShaderStage::Compute, pc);

        cmd->dispatch(1280/16, 720/16, 1);

        // Barrier: Transition to ShaderReadOnly for Fragment Read
        {
            renderer::rhi::RHIMemoryBarrier barrier{};
            barrier.texture = m_texture.get();
            barrier.srcAccessStage = renderer::rhi::ShaderStage::Compute;
            barrier.dstAccessStage = renderer::rhi::ShaderStage::Fragment;
            barrier.oldLayout = renderer::rhi::ResourceLayout::General;
            barrier.newLayout = renderer::rhi::ResourceLayout::ShaderReadOnly;
            cmd->pipelineBarrier(renderer::rhi::ShaderStage::Compute, renderer::rhi::ShaderStage::Fragment, {barrier});
        }
    }

    void recordGraphics(const renderer::RHIFrameContext& ctx) {
        auto cmd = ctx.commandBuffer;
        m_renderer->bindPipeline(cmd, m_graphicsPipeline);
        
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        cmd->bindDescriptorSet(m_renderer->pipeline(m_graphicsPipeline), 1, bindlessSet);

        GraphicsPushConstants pc { m_sampledIndex, m_samplerIndex, m_time };
        m_renderer->pushConstants(cmd, m_graphicsPipeline, renderer::rhi::ShaderStage::Fragment, pc);

        cmd->draw(3, 1, 0, 0);
    }
    
    void onRenderFrame(float deltaTime) override {
        m_renderer->beginFrame(deltaTime);
        m_renderer->drawFrame();
        m_renderer->endFrame();
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    ComputeTextureApp app;
    return app.run();
}

