#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_shader.hpp" // Use the RHI shader abstraction
#include <glm/gtc/matrix_transform.hpp>

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "generated/skybox.vert.h"

namespace pnkr::renderer::scene
{
    void Skybox::init(RHIRenderer& renderer, const std::vector<std::filesystem::path>& faces)
    {
        m_renderer = &renderer;
        m_cubemapHandle = m_renderer->createCubemap(faces, false);

        if (!m_cubemapHandle)
        {
            core::Logger::error("Failed to create skybox cubemap");
            return;
        }

        // 2. Create the pipeline immediately
        createSkyboxPipeline();

        core::Logger::info("Skybox initialized. Handle: {}", m_cubemapHandle.id);
    }

    void Skybox::destroy()
    {
        // In a real engine, you'd release the TextureHandle and PipelineHandle
        // back to the renderer here.
        m_cubemapHandle = INVALID_TEXTURE_HANDLE;
        m_pipeline = INVALID_PIPELINE_HANDLE;
    }

    void Skybox::createSkyboxPipeline()
    {
        // 1. Load Shaders using the RHI Shader abstraction
        // This handles reflection automatically
        auto vertShader = rhi::Shader::load(rhi::ShaderStage::Vertex, "shaders/skybox.vert.spv");
        auto fragShader = rhi::Shader::load(rhi::ShaderStage::Fragment, "shaders/skybox.frag.spv");

        if (!vertShader || !fragShader)
        {
            core::Logger::error("Failed to load skybox shaders");
            return;
        }

        // 2. Configure Pipeline
        // We use the RHI Pipeline Builder helper to make this clean
        rhi::RHIPipelineBuilder builder;

        builder.setShaders(vertShader.get(), fragShader.get(), nullptr)
               .setTopology(rhi::PrimitiveTopology::TriangleList)
               .setPolygonMode(rhi::PolygonMode::Fill)
               // Cull Front because we are inside the cube
               .setCullMode(rhi::CullMode::Front, true)
               // Depth: Lequal so it draws at the far plane (z=1.0)
               .enableDepthTest(false, rhi::CompareOp::LessOrEqual)
               .setNoBlend()
               .setColorFormat(m_renderer->getDrawColorFormat());
        // Note: If using dynamic rendering, depth format is also needed
        // Depending on RHI implementation, might need .setDepthFormat(...)

        // 3. Build
        // The builder automatically merges the Bindless Layout from the shader reflection
        // provided your shaders utilize the bindless sets (set=1).
        auto desc = builder.buildGraphics();

        // Ensure depth format is set explicitly if the builder didn't do it
        desc.depthFormat = m_renderer->getDrawDepthFormat();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void Skybox::draw(rhi::RHICommandBuffer* cmd, const Camera& camera)
    {
        if (!m_cubemapHandle || !m_pipeline || (m_renderer == nullptr))
        {
            return;
        }

        // 1. Get underlying RHI objects
        rhi::RHIPipeline* rhiPipe = m_renderer->getPipeline(m_pipeline);
        if (rhiPipe == nullptr)
        {
            return;
        }

        // 2. Bind Pipeline
        cmd->bindPipeline(rhiPipe);

        // 3. Bind Bindless Global Set (Set 1)
        if (m_renderer->isBindlessEnabled())
        {
            // Retrieve raw pointer to the bindless descriptor set from the device
            void* rawSet = m_renderer->device()->getBindlessDescriptorSetNative();

            // Use the overload that takes a void* (native handle)
            cmd->bindDescriptorSet(rhiPipe, 1, rawSet);
        }

        // 4. Push Constants
        ShaderGen::SkyboxPushConstants pc{};
        pc.view = glm::mat4(glm::mat3(camera.view())); // Remove translation
        pc.proj = camera.proj();
        pc.textureIndex = m_renderer->getTextureBindlessIndex(m_cubemapHandle);

        cmd->pushConstants(rhiPipe,
                           rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                           0,
                           sizeof(pc),
                           &pc);

        // 5. Draw
        cmd->draw(3, 1, 0, 0);
    }
} // namespace pnkr::renderer::scene
