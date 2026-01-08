#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/renderer/AssetManager.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/passes/RenderPassUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/renderer/gpu_shared/SkyboxShared.h"
#include "pnkr/renderer/environment/EnvironmentProcessor.hpp"

namespace pnkr::renderer::scene
{
    void Skybox::init(RHIRenderer& renderer, const std::vector<std::filesystem::path>& faces)
    {
        m_renderer = &renderer;
        if (auto* assets = m_renderer->assets()) {
            m_cubemapHandle = assets->createCubemap(faces, false);
        }

        if (!m_cubemapHandle)
        {
            core::Logger::Scene.error("Failed to create skybox cubemap");
            return;
        }

        createSkyboxPipeline();

        core::Logger::Scene.info("Skybox initialized. Handle: {}", util::u32(m_cubemapHandle.index));
    }

    void Skybox::init(RHIRenderer& renderer, TextureHandle cubemap)
    {
        m_renderer = &renderer;
        m_cubemapHandle = cubemap;

        if (!m_cubemapHandle)
        {
            core::Logger::Scene.error("Failed to initialize skybox cubemap");
            return;
        }

        createSkyboxPipeline();

        core::Logger::Scene.info("Skybox initialized from cubemap. Handle: {}", util::u32(m_cubemapHandle.index));
    }

    void Skybox::initFromEquirectangular(RHIRenderer& renderer, const std::filesystem::path& path)
    {
        m_renderer = &renderer;

        TexturePtr equiTex;
        if (auto* assets = m_renderer->assets()) {
            equiTex = assets->loadTexture(path, false);
        }

        if (!equiTex.isValid()) {
            core::Logger::Scene.error("Failed to load equirectangular texture: {}", path.string());
            return;
        }

        EnvironmentProcessor processor(m_renderer);
        m_cubemapHandle = processor.convertEquirectangularToCubemap(equiTex.handle());

        if (!m_cubemapHandle) {
            core::Logger::Scene.error("Failed to convert equirectangular to cubemap: {}", path.string());
            return;
        }

        createSkyboxPipeline();

        core::Logger::Scene.info("Skybox initialized from equirectangular HDR. Handle: {}", util::u32(m_cubemapHandle.index));
    }

    void Skybox::destroy()
    {
        m_cubemapHandle = INVALID_TEXTURE_HANDLE;
        m_pipeline = {};
    }

    void Skybox::createSkyboxPipeline()
    {
        using namespace passes::utils;
        auto shaders = loadGraphicsShaders("shaders/skybox.vert.spv", "shaders/skybox.frag.spv", "Skybox");
        if (!shaders.success)
        {
            return;
        }

        rhi::RHIPipelineBuilder builder;

        builder
            .setShaders(shaders.vertex.get(), shaders.fragment.get(), nullptr)
            .setTopology(rhi::PrimitiveTopology::TriangleList)
            .setPolygonMode(rhi::PolygonMode::Fill)
            .setCullMode(rhi::CullMode::None, false)
            .setMultisampling(m_msaaSamples, m_msaaSamples > 1, 0.0F)
            .enableDepthTest(true, rhi::CompareOp::LessOrEqual)
            .setNoBlend()
            .setColorFormat(rhi::Format::B10G11R11_UFLOAT_PACK32);

        auto desc = builder.buildGraphics();

        desc.depthFormat = m_renderer->getDrawDepthFormat();

        m_pipeline = m_renderer->createGraphicsPipeline(desc);
    }

    void Skybox::draw(rhi::RHICommandList* cmd, const Camera& camera)
    {
        if (!m_cubemapHandle || !m_pipeline || (m_renderer == nullptr))
        {
            static bool warnedOnce = false;
            if (!warnedOnce) {
                if (!m_cubemapHandle) core::Logger::Render.warn("Skybox::draw: m_cubemapHandle invalid");
                if (!m_pipeline) core::Logger::Render.warn("Skybox::draw: m_pipeline invalid");
                warnedOnce = true;
            }
            return;
        }

        rhi::RHIPipeline* rhiPipe = m_renderer->getPipeline(m_pipeline);
        if (rhiPipe == nullptr)
        {
            static bool pipeWarnedOnce = false;
            if (!pipeWarnedOnce) {
                core::Logger::Render.warn("Skybox::draw: Failed to retrieve pipeline");
                pipeWarnedOnce = true;
            }
            return;
        }

        cmd->bindPipeline(rhiPipe);

        gpu::SkyboxPushConstants pc{};
        pc.invViewProj = glm::inverse(camera.proj() * glm::mat4(glm::mat3(camera.view())));
        pc.textureIndex = static_cast<uint32_t>(m_renderer->getTextureBindlessIndex(m_cubemapHandle));
        pc.samplerIndex = static_cast<uint32_t>(m_renderer->getBindlessSamplerIndex(rhi::SamplerAddressMode::ClampToEdge));
        pc.flipY = m_flipY ? 1 : 0;
        pc.rotation = glm::radians(m_rotation);

        cmd->pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, pc);

        if (m_renderer->isBindlessEnabled())
        {
            rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
            cmd->bindDescriptorSet(1, bindlessSet);
        }

        cmd->draw(3, 1, 0, 0);
    }

    void Skybox::resize(uint32_t msaaSamples)
    {
        if (m_msaaSamples != msaaSamples)
        {
            m_msaaSamples = msaaSamples;
            if (m_renderer != nullptr) {
              createSkyboxPipeline();
            }
        }
    }

    bool Skybox::isValid() const
    {
        return m_cubemapHandle.isValid() && m_pipeline.isValid();
    }
}
