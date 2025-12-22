#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/renderer/scene/GLTFUnified.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "../common/RhiSampleApp.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

// INCLUDE GENERATED HEADERS from ShaderStructGen
#include "generated/gltf.frag.h"
#include "generated/gltf.vert.h"

using namespace pnkr;
using namespace pnkr::renderer::scene;

class UnifiedGLTFSample : public samples::RhiSampleApp {
public:
    UnifiedGLTFSample() : samples::RhiSampleApp({
        .title = "PNKR - Unified glTF Renderer",
        .width = 1824,
        .height = 928,
        .createRenderer = false
    }) {}

    GLTFUnifiedContext m_ctx;
    Camera m_camera;
    CameraController m_cameraController{{0.0f, 0.0f, -2.5f}, 90.0f, 0.0f};
    std::unique_ptr<InfiniteGrid> m_grid;
    std::unique_ptr<Skybox> m_skybox;
    
    float m_rotation = 0.0f;
    TextureHandle m_irradiance, m_prefilter, m_brdfLut;

    void onInit() override {
        renderer::RendererConfig config;
        config.m_enableBindless = true;
        m_renderer = std::make_unique<renderer::RHIRenderer>(m_window, config);

        // 1. Load IBL Maps
        m_brdfLut = m_renderer->loadTextureKTX("assets/brdf_lut.ktx2");
        m_irradiance = m_renderer->loadTextureKTX("assets/piazza_bologni_1k_irradiance.ktx");
        m_prefilter = m_renderer->loadTextureKTX("assets/piazza_bologni_1k_prefilter.ktx");

        // 2. Load GLTF Model via Unified Context
        loadGLTF(m_ctx, *m_renderer, "assets/ClearcoatWicker/glTF-Binary/ClearcoatWicker.glb");
        if (!m_ctx.model) throw cpptrace::runtime_error("Failed to load ClearcoatWicker.glb");

        uploadMaterials();
        uploadEnvironments();

        // 3. Create Pipelines
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("gltf.vert.spv"));
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("gltf.frag.spv"));

        m_ctx.pipelineSolid = m_renderer->createGraphicsPipeline(
            renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .useVertexType<renderer::Vertex>()
            .setCullMode(renderer::rhi::CullMode::Back, true)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("PBRPipeline")
            .buildGraphics()
        );

        m_ctx.pipelineTransparent = m_renderer->createGraphicsPipeline(
            renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), nullptr)
            .useVertexType<renderer::Vertex>()
            .setCullMode(renderer::rhi::CullMode::None, true)
            .enableDepthTest(false) // No depth write for transparency
            .setAlphaBlend()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("PBRTransparentPipeline")
            .buildGraphics()
        );

        m_cameraController.applyToCamera(m_camera);
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        m_grid = std::make_unique<InfiniteGrid>();
        m_grid->init(*m_renderer);

        auto skyboxKtx = resolveSkyboxKtx();
        if (!skyboxKtx.empty())
        {
            auto skyboxHandle = m_renderer->loadTextureKTX(skyboxKtx);
            if (skyboxHandle != INVALID_TEXTURE_HANDLE)
            {
                m_skybox = std::make_unique<Skybox>();
                m_skybox->init(*m_renderer, skyboxHandle);
            }
        }

        initUI();
    }

    void uploadMaterials() {
        std::vector<ShaderGen::gltf_frag::MetallicRoughnessDataGPU> gpuData;
        for (const auto& mat : m_ctx.model->materials()) {
            ShaderGen::gltf_frag::MetallicRoughnessDataGPU d{};

            d.baseColorFactor = mat.m_baseColorFactor;
            d.emissiveFactorAlphaCutoff = glm::vec4(mat.m_emissiveFactor, mat.m_alphaCutoff);
            d.alphaMode = mat.m_alphaMode;
            

            d.specularFactorWorkflow = glm::vec4(mat.m_specularFactor, mat.m_isSpecularGlossiness ? 1.0f : 0.0f);

            d.metallicRoughnessNormalOcclusion = glm::vec4(
                mat.m_metallicFactor,
                mat.m_roughnessFactor,
                mat.m_normalScale,
                mat.m_occlusionStrength
            );

            auto resolveTexture = [&](TextureHandle handle) -> uint32_t {
                if (handle == INVALID_TEXTURE_HANDLE) return 0xFFFFFFFFu;
                return m_renderer->getTextureBindlessIndex(handle);
            };

            auto resolveSampler = [&](renderer::rhi::SamplerAddressMode mode) -> uint32_t {
                return m_renderer->getBindlessSamplerIndex(mode);
            };

            d.occlusionTexture = resolveTexture(mat.m_occlusionTexture);
            d.occlusionTextureSampler = resolveSampler(mat.m_occlusionSampler);
            d.occlusionTextureUV = mat.m_occlusionUV;
            d.emissiveTexture = resolveTexture(mat.m_emissiveTexture);
            d.emissiveTextureSampler = resolveSampler(mat.m_emissiveSampler);
            d.emissiveTextureUV = mat.m_emissiveUV;
            d.baseColorTexture = resolveTexture(mat.m_baseColorTexture);
            d.baseColorTextureSampler = resolveSampler(mat.m_baseColorSampler);
            d.baseColorTextureUV = mat.m_baseColorUV;
            d.normalTexture = resolveTexture(mat.m_normalTexture);
            d.normalTextureSampler = resolveSampler(mat.m_normalSampler);
            d.normalTextureUV = mat.m_normalUV;
            d.metallicRoughnessTexture = resolveTexture(mat.m_metallicRoughnessTexture);
            d.metallicRoughnessTextureSampler = resolveSampler(mat.m_metallicRoughnessSampler);
            d.metallicRoughnessTextureUV = mat.m_metallicRoughnessUV;

            gpuData.push_back(d);
        }

        auto buffer =  m_renderer->createBuffer({
            .size = gpuData.size() * sizeof(ShaderGen::gltf_frag::MetallicRoughnessDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = gpuData.data(),
            .debugName = "Unified Materials"
        });
        m_ctx.materialBuffer = buffer;
    }

    void uploadEnvironments() {
        ShaderGen::gltf_frag::EnvironmentMapDataGPU env{};
        env.envMapTexture = m_renderer->getTextureBindlessIndex(m_prefilter);
        env.envMapTextureSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(m_irradiance);
        env.envMapTextureIrradianceSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.texBRDF_LUT = m_renderer->getTextureBindlessIndex(m_brdfLut);
        env.texBRDF_LUTSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);

        m_ctx.environmentBuffer = m_renderer->createBuffer({
            .size = sizeof(ShaderGen::gltf_frag::EnvironmentMapDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = &env,
            .debugName = "Unified Environments"
        });
    }

    void onUpdate(float dt) override {
        m_cameraController.update(m_input, dt);
        m_cameraController.applyToCamera(m_camera);
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        auto* cmd = ctx.commandBuffer;

        if (m_skybox) m_skybox->draw(cmd, m_camera);

        // Build/Update transforms
        buildTransformsList(m_ctx);
        sortTransparentNodes(m_ctx, m_camera.position());

        // Bind Global Bindless Set (Set 1)
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        cmd->bindDescriptorSet(m_renderer->pipeline(m_ctx.pipelineSolid), 1, bindlessSet);

        // Common Push Constants
        ShaderGen::gltf_frag::PerFrameData pc {};
        pc.drawable.view = m_camera.view();
        pc.drawable.proj = m_camera.proj();
        pc.drawable.cameraPos = glm::vec4(m_camera.position(), 1.0f);
        pc.drawable.transformBufferPtr = m_renderer->getBuffer(m_ctx.transformBuffer)->getDeviceAddress();
        pc.drawable.materialBufferPtr = m_renderer->getBuffer(m_ctx.materialBuffer)->getDeviceAddress();
        pc.drawable.environmentBufferPtr = m_renderer->getBuffer(m_ctx.environmentBuffer)->getDeviceAddress();
        pc.drawable.envId = 0u;
        pc.drawable.transmissionTexture = 0xFFFFFFFFu;
        pc.drawable.transmissionSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);

        auto drawTransform = [&](uint32_t xformId, PipelineHandle pipeline) {
            const auto& x = m_ctx.transforms[xformId];
            const auto& node = m_ctx.model->nodes()[x.nodeIndex];
            const auto& prim = node.m_meshPrimitives[x.primIndex];

            m_renderer->pushConstants(cmd, pipeline,
                renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment, pc);
            
            m_renderer->bindMesh(cmd, prim.m_mesh);
            // xformId passed as firstInstance -> gl_BaseInstance
            cmd->drawIndexed(m_renderer->getMeshIndexCount(prim.m_mesh), 1, 0, 0, xformId);
        };

        // 1. Opaque
        m_renderer->bindPipeline(cmd, m_ctx.pipelineSolid);
        for (uint32_t xformId : m_ctx.opaque) drawTransform(xformId, m_ctx.pipelineSolid);
        for (uint32_t xformId : m_ctx.transmission) drawTransform(xformId, m_ctx.pipelineSolid);

        // 2. Transparent
        if (!m_ctx.transparent.empty()) {
            m_renderer->bindPipeline(cmd, m_ctx.pipelineTransparent);
            for (uint32_t xformId : m_ctx.transparent) drawTransform(xformId, m_ctx.pipelineTransparent);
        }

        if (m_grid) m_grid->draw(cmd, m_camera);
    }

private:
    std::filesystem::path resolveSkyboxKtx() const {
        const std::vector<std::filesystem::path> candidates = {
            "assets/skybox.ktx", "assets/skybox.ktx2"
        };
        for (const auto& path : candidates) if (std::filesystem::exists(path)) return path;
        return {};
    }
};

int main() {
    UnifiedGLTFSample app;
    return app.run();
}
