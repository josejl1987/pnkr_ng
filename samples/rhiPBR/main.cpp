#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/CameraController.hpp"
#include "pnkr/renderer/scene/InfiniteGrid.hpp"
#include "pnkr/renderer/scene/Skybox.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/app/Application.hpp"
#include "pnkr/core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

#include "pnkr/renderer/scene/MaterialDataGPU.hpp"

// INCLUDE GENERATED HEADERS from ShaderStructGen
#include "pnkr/generated/gltf.frag.h"
#include "generated/pbr.frag.h"

using namespace pnkr;
using namespace pnkr::renderer::scene;


class PBRSample : public app::Application {
public:
    PBRSample() : app::Application({
        .title = "PNKR - Metallic Roughness PBR",
        .width = 1824,
        .height = 928,
        .createRenderer = false
    }) {}

    std::unique_ptr<Model> m_model;
    Camera m_camera;
    CameraController m_cameraController{{0.0f, 0.0f, -2.5f}, 90.0f, 0.0f};
    std::unique_ptr<InfiniteGrid> m_grid;
    std::unique_ptr<Skybox> m_skybox;
    PipelineHandle m_pipeline;
    PipelineHandle m_transparentPipeline;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_materialBuffer;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_environmentBuffer;

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

        // 2. Load GLTF Model
        m_model = renderer::scene::Model::load(*m_renderer, "assets/SpecGlossVsMetalRough.glb", false);
        if (!m_model) throw cpptrace::runtime_error("Failed to load SpecGlossVsMetalRough.glb");

        uploadMaterials();
        uploadEnvironments();

        // 3. Create Pipelines
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("pbr.vert.spv"));
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("pbr.frag.spv"));

        m_pipeline = m_renderer->createGraphicsPipeline(
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

        m_transparentPipeline = m_renderer->createGraphicsPipeline(
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
        else
        {
            core::Logger::warn("PBR skybox KTX not found, skipping skybox");
        }

        initUI(); // Initialize Sample UI (Vsync/FPS)
    }

    void uploadMaterials() {
        std::vector<ShaderGen::pbr_frag::MetallicRoughnessDataGPU> gpuData;
        for (const auto& mat : m_model->materials()) {
            ShaderGen::pbr_frag::MetallicRoughnessDataGPU d{};

            // Common
            d.baseColorFactor = mat.m_baseColorFactor;
            d.emissiveFactorAlphaCutoff = glm::vec4(mat.m_emissiveFactor, mat.m_alphaCutoff);
            d.alphaMode = mat.m_alphaMode;

            auto resolveTexture = [&](TextureHandle handle) -> uint32_t {
                if (handle == INVALID_TEXTURE_HANDLE) {
                    return 0xFFFFFFFFu;
                }
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

            // --- Workflow Specifics ---
            if (mat.m_isSpecularGlossiness) {
                d.specularFactorWorkflow = glm::vec4(mat.m_specularFactor, 1.0f); // Flag = 1.0

                // Map Glossiness to the Roughness slot (y)
                d.metallicRoughnessNormalOcclusion = glm::vec4(
                    0.0f,
                    mat.m_glossinessFactor,
                    mat.m_normalScale,
                    mat.m_occlusionStrength
                );

                // Map SpecularGlossiness texture to the MetallicRoughness slot
                d.metallicRoughnessTexture = resolveTexture(mat.m_metallicRoughnessTexture);
            } else {
                d.specularFactorWorkflow = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Flag = 0.0

                d.metallicRoughnessNormalOcclusion = glm::vec4(
                    mat.m_metallicFactor,
                    mat.m_roughnessFactor,
                    mat.m_normalScale,
                    mat.m_occlusionStrength
                );

                d.metallicRoughnessTexture = resolveTexture(mat.m_metallicRoughnessTexture);
            }

            d.metallicRoughnessTextureSampler = resolveSampler(mat.m_metallicRoughnessSampler);
            d.metallicRoughnessTextureUV = mat.m_metallicRoughnessUV;

            gpuData.push_back(d);
        }

        m_materialBuffer = m_renderer->device()->createBuffer({
            .size = gpuData.size() * sizeof(ShaderGen::pbr_frag::MetallicRoughnessDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = gpuData.data()
        });
    }

    void uploadEnvironments() {
        ShaderGen::pbr_frag::EnvironmentMapDataGPU env{};
        env.envMapTexture = m_renderer->getTextureBindlessIndex(m_prefilter);
        env.envMapTextureSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(m_irradiance);
        env.envMapTextureIrradianceSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.texBRDF_LUT = m_renderer->getTextureBindlessIndex(m_brdfLut);
        env.texBRDF_LUTSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);

        m_environmentBuffer = m_renderer->device()->createBuffer({
            .size = sizeof(ShaderGen::pbr_frag::EnvironmentMapDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = &env
        });
    }

    void onUpdate(float dt) override {
        m_rotation += 0;
        m_cameraController.update(m_input, dt);
        m_cameraController.applyToCamera(m_camera);
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        auto* cmd = ctx.commandBuffer;

        if (m_skybox)
        {
            m_skybox->draw(cmd, m_camera);
        }


        m_renderer->bindPipeline(cmd, m_pipeline);

        // Bind Global Bindless Set (Set 1)
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, bindlessSet);

        // USE GENERATED STRUCT: ShaderStructGen handles the padding automatically.
        ShaderGen::pbr_frag::PerFrameData pc {};

        pc.drawable.view = m_camera.view();
        pc.drawable.proj = m_camera.proj();
        pc.drawable.cameraPos = glm::vec4(m_camera.position(), 1.0f);
        pc.drawable.envId = 0u;
        pc.materials = m_materialBuffer->getDeviceAddress();
        pc.environments = m_environmentBuffer->getDeviceAddress();

        // Apply global rotation to the whole model
        glm::mat4 globalModel = glm::rotate(glm::mat4(1.0f), m_rotation, {0, 1, 0});

        struct DrawCall {
            glm::mat4 model;
            uint32_t matId;
            MeshHandle mesh;
            float depth;
        };
        std::vector<DrawCall> opaqueDraws;
        std::vector<DrawCall> transparentDraws;

        // Hierarchy traversal to collect draw calls
        std::function<void(int)> collectDraws = [&](int nodeIdx) {
            const auto& node = m_model->nodes()[nodeIdx];
            glm::mat4 worldMat = globalModel * node.m_worldTransform.mat4();

            for (const auto& prim : node.m_meshPrimitives) {
                const auto& mat = m_model->materials()[prim.m_materialIndex];
                
                DrawCall dc{worldMat, prim.m_materialIndex, prim.m_mesh, 0.0f};

                if (mat.m_alphaMode == 2) { // BLEND
                    // Calculate depth for sorting (back-to-front)
                    glm::vec4 pos = worldMat * glm::vec4(0,0,0,1);
                    dc.depth = glm::length(m_camera.position() - glm::vec3(pos));
                    transparentDraws.push_back(dc);
                } else {
                    opaqueDraws.push_back(dc);
                }
            }

            for (int child : node.m_children) collectDraws(child);
        };

        for (int root : m_model->rootNodes()) collectDraws(root);

        // 1. Draw Opaque
        m_renderer->bindPipeline(cmd, m_pipeline);
        for (const auto& dc : opaqueDraws) {
            pc.drawable.model = dc.model;
            pc.drawable.matId = dc.matId;
            m_renderer->pushConstants(cmd, m_pipeline,
                renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment, pc);
            m_renderer->bindMesh(cmd, dc.mesh);
            m_renderer->drawMesh(cmd, dc.mesh);
        }

        // 2. Draw Transparent (Back-to-front)
        std::sort(transparentDraws.begin(), transparentDraws.end(), [](const auto& a, const auto& b) {
            return a.depth > b.depth;
        });

        if (!transparentDraws.empty()) {
            m_renderer->bindPipeline(cmd, m_transparentPipeline);
            for (const auto& dc : transparentDraws) {
                pc.drawable.model = dc.model;
                pc.drawable.matId = dc.matId;
                m_renderer->pushConstants(cmd, m_transparentPipeline,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment, pc);
                m_renderer->bindMesh(cmd, dc.mesh);
                m_renderer->drawMesh(cmd, dc.mesh);
            }
        }

        if (m_grid)
        {
            m_grid->draw(cmd, m_camera);
        }

    }

private:
    std::filesystem::path resolveSkyboxKtx() const
    {
        const std::vector<std::filesystem::path> candidates = {
            std::filesystem::path("assets/skybox.ktx"),
            baseDir() / "assets/skybox.ktx",
            std::filesystem::path("assets/skybox.ktx2"),
            baseDir() / "assets/skybox.ktx2"
        };

        for (const auto& path : candidates)
        {
            if (std::filesystem::exists(path))
            {
                return path;
            }
        }

        return {};
    }
};

int main() {
    PBRSample app;
    return app.run();
}

