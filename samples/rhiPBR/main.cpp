#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "../common/RhiSampleApp.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "pnkr/renderer/scene/MaterialDataGPU.hpp"

// INCLUDE GENERATED HEADER from ShaderStructGen
#include "generated/pbr.vert.h"

using namespace pnkr;
using namespace pnkr::renderer::scene;

// Manual definition required because it's a pointer in the shader (ShaderStructGen recursion stops at pointers)
struct EnvironmentMapDataGPU {
    uint32_t envMapTexture;
    uint32_t envMapTextureSampler;
    uint32_t envMapTextureIrradiance;
    uint32_t envMapTextureIrradianceSampler;
    uint32_t texBRDF_LUT;
    uint32_t texBRDF_LUTSampler;
    uint32_t unused0;
    uint32_t unused1;
};

// REMOVED: Manual definitions of PerDrawData and PBRpushConstant.
// We strictly use ShaderGen::PerFrameData and ShaderGen::PerDrawData to guarantee memory layout match.

class PBRSample : public samples::RhiSampleApp {
public:
    PBRSample() : samples::RhiSampleApp({
        .title = "PNKR - Metallic Roughness PBR",
        .width = 1824,
        .height = 928,
        .createRenderer = false
    }) {}

    std::unique_ptr<Model> m_model;
    Camera m_camera;
    PipelineHandle m_pipeline;
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
        m_model = renderer::scene::Model::load(*m_renderer, "assets/DamagedHelmet.glb", false);
        if (!m_model) throw cpptrace::runtime_error("Failed to load DamagedHelmet.glb");

        uploadMaterials();
        uploadEnvironments();

        // 3. Create PBR Pipeline
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

        m_camera.lookAt({0, 0, -2.5}, {0, 0, 0});
        initUI(); // Initialize Sample UI (Vsync/FPS)
    }

    void uploadMaterials() {
        std::vector<MaterialDataGPU> gpuData;
        for (const auto& mat : m_model->materials()) {
            MaterialDataGPU d{};
            d.baseColorFactor = mat.m_baseColorFactor;
            d.metallicRoughnessNormalOcclusion =
                glm::vec4(mat.m_metallicFactor, mat.m_roughnessFactor, mat.m_normalScale, mat.m_occlusionStrength);
            d.emissiveFactorAlphaCutoff = glm::vec4(mat.m_emissiveFactor, mat.m_alphaCutoff);

            auto resolveTexture = [&](TextureHandle handle) -> uint32_t {
                if (handle == INVALID_TEXTURE_HANDLE) {
                    return 0u;
                }
                return m_renderer->getTextureBindlessIndex(handle);
            };

            d.occlusionTexture = resolveTexture(mat.m_occlusionTexture);
            d.occlusionTextureSampler = m_renderer->getBindlessSamplerIndex(mat.m_occlusionSampler);
            d.occlusionTextureUV = mat.m_occlusionUV;
            d.emissiveTexture = resolveTexture(mat.m_emissiveTexture);
            d.emissiveTextureSampler = m_renderer->getBindlessSamplerIndex(mat.m_emissiveSampler);
            d.emissiveTextureUV = mat.m_emissiveUV;
            d.baseColorTexture = resolveTexture(mat.m_baseColorTexture);
            d.baseColorTextureSampler = m_renderer->getBindlessSamplerIndex(mat.m_baseColorSampler);
            d.baseColorTextureUV = mat.m_baseColorUV;
            d.metallicRoughnessTexture = resolveTexture(mat.m_metallicRoughnessTexture);
            d.metallicRoughnessTextureSampler = m_renderer->getBindlessSamplerIndex(mat.m_metallicRoughnessSampler);
            d.metallicRoughnessTextureUV = mat.m_metallicRoughnessUV;
            d.normalTexture = resolveTexture(mat.m_normalTexture);
            d.normalTextureSampler = m_renderer->getBindlessSamplerIndex(mat.m_normalSampler);
            d.normalTextureUV = mat.m_normalUV;
            d.alphaMode = mat.m_alphaMode;

            gpuData.push_back(d);
        }

        m_materialBuffer = m_renderer->device()->createBuffer({
            .size = gpuData.size() * sizeof(MaterialDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = gpuData.data()
        });
    }

    void uploadEnvironments() {
        EnvironmentMapDataGPU env{};
        env.envMapTexture = m_renderer->getTextureBindlessIndex(m_prefilter);
        env.envMapTextureSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.envMapTextureIrradiance = m_renderer->getTextureBindlessIndex(m_irradiance);
        env.envMapTextureIrradianceSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);
        env.texBRDF_LUT = m_renderer->getTextureBindlessIndex(m_brdfLut);
        env.texBRDF_LUTSampler = m_renderer->getBindlessSamplerIndex(renderer::rhi::SamplerAddressMode::ClampToEdge);

        m_environmentBuffer = m_renderer->device()->createBuffer({
            .size = sizeof(EnvironmentMapDataGPU),
            .usage = renderer::rhi::BufferUsage::StorageBuffer | renderer::rhi::BufferUsage::ShaderDeviceAddress,
            .memoryUsage = renderer::rhi::MemoryUsage::CPUToGPU,
            .data = &env
        });
    }

    void onUpdate(float dt) override {
        m_rotation += 0;
        float aspect = (float)m_window.width() / m_window.height();
        m_camera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        auto* cmd = ctx.commandBuffer;
        m_renderer->bindPipeline(cmd, m_pipeline);

        // Bind Global Bindless Set (Set 1)
        renderer::rhi::RHIDescriptorSet* bindlessSet = m_renderer->device()->getBindlessDescriptorSet();
        ctx.commandBuffer->bindDescriptorSet(m_renderer->pipeline(m_pipeline), 1, bindlessSet);

        // USE GENERATED STRUCT: ShaderStructGen handles the padding automatically.
        ShaderGen::PerFrameData pc {};

        pc.drawable.view = m_camera.view();
        pc.drawable.proj = m_camera.proj();
        pc.drawable.cameraPos = glm::vec4(m_camera.position(), 1.0f);
        pc.drawable.envId = 0u;
        pc.materials = m_materialBuffer->getDeviceAddress();
        pc.environments = m_environmentBuffer->getDeviceAddress();

        // Apply global rotation to the whole model
        glm::mat4 globalModel = glm::rotate(glm::mat4(1.0f), m_rotation, {0, 1, 0});

        // FIX: Removed the 90-degree X-axis rotation which was causing Y/Z swapping
        // globalModel = glm::rotate(globalModel, glm::radians(90.0f), {1, 0, 0});

        // Hierarchy draw
        std::function<void(int)> drawNode = [&](int nodeIdx) {
            const auto& node = m_model->nodes()[nodeIdx];

            for (const auto& prim : node.m_meshPrimitives) {
                pc.drawable.model = globalModel * node.m_worldTransform.mat4();
                pc.drawable.matId = prim.m_materialIndex;

                m_renderer->pushConstants(cmd, m_pipeline,
                    renderer::rhi::ShaderStage::Vertex | renderer::rhi::ShaderStage::Fragment, pc);

                m_renderer->bindMesh(cmd, prim.m_mesh);
                m_renderer->drawMesh(cmd, prim.m_mesh);
            }

            for (int child : node.m_children) drawNode(child);
        };

        for (int root : m_model->rootNodes()) drawNode(root);
    }
};

int main() {
    PBRSample app;
    return app.run();
}