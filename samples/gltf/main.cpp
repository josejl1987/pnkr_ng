#include "../common/SampleApp.h"

#include <filesystem>
#include <imgui.h>

#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/scene/Scene.hpp"
#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/pipeline/PipelineBuilder.h"
#include "pnkr/renderer/vulkan/pipeline/compute_pipeline.hpp"
#include "pnkr/ui/imgui_layer.hpp"

using namespace pnkr;
using namespace pnkr::samples;
using namespace pnkr::renderer;

class GltfSample : public SampleApp {
    scene::Scene m_scene;
    std::unique_ptr<scene::Model> m_model;
    PipelineHandle m_gfxPipeline{};

    std::unique_ptr<ComputePipeline> m_toneMapPipeline;
    vk::DescriptorPool m_descriptorPool{};
    vk::DescriptorSetLayout m_toneMapLayout{};
    vk::Sampler m_hdrSampler{};
    std::vector<vk::DescriptorSet> m_toneMapSets;

    ui::ImGuiLayer m_ui;
    bool m_uiCapturedMouse{false};

public:
    GltfSample() : SampleApp({"PNKR GLTF + Compute", 1280, 720}) {}

    void onInit() override {
        m_ui.init(m_renderer, &m_window);

        const auto assetDir = baseDir() / "assets";
        const auto modelPath = assetDir / "Duck.glb";
        if (!std::filesystem::exists(modelPath)) {
            throw std::runtime_error("Model not found at: " + modelPath.string());
        }

        m_model = scene::Model::load(m_renderer, modelPath);
        if (!m_model) {
            throw std::runtime_error("Failed to load model");
        }

        setupToneMapping();
        setupGraphicsPipeline();

        m_scene.cameraController().setPosition({0.0f, 1.0f, 3.0f});
        m_window.setRelativeMouseMode(true);
    }

    void onUpdate(float /*dt*/) override {
        m_ui.beginFrame();

        ImGui::Begin("Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        if (m_input.isKeyDown(SDL_SCANCODE_ESCAPE)) {
            m_window.setRelativeMouseMode(false);
            m_uiCapturedMouse = true;
        }
        if (m_uiCapturedMouse && !ImGui::GetIO().WantCaptureMouse &&
            m_input.isMouseButtonDown(SDL_BUTTON_LEFT)) {
            m_window.setRelativeMouseMode(true);
            m_uiCapturedMouse = false;
        }

        ImGui::End();
        m_ui.endFrame();
    }

    void onEvent(const SDL_Event& event) override {
        m_ui.handleEvent(event);
    }

    void onRender(const RenderFrameContext& ctx) override {
        m_scene.update(ctx.m_deltaTime, ctx.m_extent, m_input);
        if (m_model) {
            m_model->updateTransforms();
        }

        if (m_model) {
            for (const auto& node : m_model->nodes()) {
                if (!node.meshPrimitives) continue;
                PushConstants pc{};
                pc.m_model = node.worldTransform.mat4();
                pc.m_viewProj = m_scene.camera().viewProj();

                for (const auto& prim : *node.meshPrimitives) {
                    const auto& mat = m_model->materials()[prim.materialIndex];
                    m_renderer.bindPipeline(ctx.m_cmd, m_gfxPipeline);
                    vk::DescriptorSet set = m_renderer.getTextureDescriptor(mat.baseColorTexture);
                    ctx.m_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                 m_renderer.pipelineLayout(m_gfxPipeline),
                                                 0,
                                                 set,
                                                 nullptr);
                    m_renderer.pushConstants(ctx.m_cmd, m_gfxPipeline,
                                             vk::ShaderStageFlagBits::eVertex, pc);
                    m_renderer.bindMesh(ctx.m_cmd, prim.mesh);
                    m_renderer.drawMesh(ctx.m_cmd, prim.mesh);
                }
            }
        }
    }

    void onShutdown() override {
        m_renderer.device().waitIdle();
        if (m_toneMapLayout) {
            m_renderer.device().destroyDescriptorSetLayout(m_toneMapLayout);
            m_toneMapLayout = nullptr;
        }
        if (m_descriptorPool) {
            m_renderer.device().destroyDescriptorPool(m_descriptorPool);
            m_descriptorPool = nullptr;
        }
        if (m_hdrSampler) {
            m_renderer.device().destroySampler(m_hdrSampler);
            m_hdrSampler = nullptr;
        }
        m_ui.shutdown();
    }

private:
    void setupToneMapping() {
        const auto toneMapShader = getShaderPath("tonemap.comp.spv");

        vk::DescriptorPoolSize poolSizes[] = {
            {vk::DescriptorType::eCombinedImageSampler, 10},
            {vk::DescriptorType::eStorageImage, 10}
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = 10;
        m_descriptorPool = m_renderer.device().createDescriptorPool(poolInfo);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        m_hdrSampler = m_renderer.device().createSampler(samplerInfo);

        vk::DescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        m_toneMapLayout = m_renderer.device().createDescriptorSetLayout(layoutInfo);

        m_toneMapPipeline = pnkr::renderer::ComputePipelineBuilder(m_renderer)
                                .setShader(toneMapShader)
                                .addDescriptorSetLayout(m_toneMapLayout)
                                .build();

        const uint32_t swapImageCount = m_renderer.getSwapchainImageCount();
        std::vector<vk::DescriptorSetLayout> layouts(swapImageCount, m_toneMapLayout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = swapImageCount;
        allocInfo.pSetLayouts = layouts.data();

        m_toneMapSets = m_renderer.device().allocateDescriptorSets(allocInfo);

        for (uint32_t i = 0; i < swapImageCount; i++) {
            vk::DescriptorImageInfo hdrInfo{};
            hdrInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            hdrInfo.imageView = m_renderer.getOffscreenTexture().view();
            hdrInfo.sampler = m_hdrSampler;

            vk::DescriptorImageInfo swapInfo{};
            swapInfo.imageLayout = vk::ImageLayout::eGeneral;
            swapInfo.imageView = m_renderer.getSwapchainImageView(i);

            vk::WriteDescriptorSet writes[2]{};
            writes[0].dstSet = m_toneMapSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &hdrInfo;

            writes[1].dstSet = m_toneMapSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageImage;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &swapInfo;

            m_renderer.device().updateDescriptorSets(2, writes, 0, nullptr);
        }

        m_renderer.setPostProcessCallback(
            [this](vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent) {
                m_renderer.bindPipeline(cmd, *m_toneMapPipeline);

                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eCompute,
                    m_toneMapPipeline->layout(),
                    0,
                    m_toneMapSets[imageIndex],
                    nullptr);

                const uint32_t groupX = (extent.width + 15) / 16;
                const uint32_t groupY = (extent.height + 15) / 16;
                m_renderer.dispatch(cmd, groupX, groupY, 1);
            });
    }

    void setupGraphicsPipeline() {
        const auto vert = getShaderPath("gltf.vert.spv");
        const auto frag = getShaderPath("gltf.frag.spv");

        m_gfxPipeline = PipelineBuilder(m_renderer)
                            .setShaders(vert, frag)
                            .setInputTopology(vk::PrimitiveTopology::eTriangleList)
                            .setPolygonMode(vk::PolygonMode::eFill)
                            .setCullMode(vk::CullModeFlagBits::eBack)
                            .enableDepthTest(true, vk::CompareOp::eLess)
                            .addDescriptorSetLayout(m_renderer.getTextureDescriptorLayout())
                            .setPushConstantSize(sizeof(PushConstants))
                            .setPushConstantsShaderFlags(vk::ShaderStageFlagBits::eVertex)
                            .build();
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    GltfSample app;
    return app.run();
}
