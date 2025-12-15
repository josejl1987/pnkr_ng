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

class GltfBindlessSample : public SampleApp
{
    scene::Scene m_scene;
    std::unique_ptr<scene::Model> m_model;
    PipelineHandle m_gfxPipeline{};
    PipelineHandle m_gfxPipelineBindless{};

    std::unique_ptr<ComputePipeline> m_toneMapPipeline;
    vk::DescriptorPool m_descriptorPool{};
    vk::DescriptorSetLayout m_toneMapLayout{};
    vk::Sampler m_hdrSampler{};
    std::vector<vk::DescriptorSet> m_toneMapSets;

    ui::ImGuiLayer m_ui;
    bool m_uiCapturedMouse{false};

    // Bindless toggle state
    bool m_useBindless{false};

    // Performance tracking
    struct PerformanceStats
    {
        std::vector<float> frameTimesMs;
        std::vector<float> bindTimesUs;
        int frameCount{0};
        float avgFrameTime{0.0f};
        float avgBindTime{0.0f};
        int descriptorBindCount{0};
    } m_stats;

public:
    GltfBindlessSample() : SampleApp({"PNKR GLTF - Bindless Toggle", 1280, 720})
    {
        m_stats.bindTimesUs.assign(120, 0);
        m_stats.frameTimesMs.assign(120, 0);
    }

    void onInit() override
    {
        m_ui.init(m_renderer, &m_window);

        const auto assetDir = baseDir() / "assets";
        const auto modelPath = assetDir / "duck.glb";
        if (!std::filesystem::exists(modelPath))
        {
            throw std::runtime_error("Model not found at: " + modelPath.string());
        }

        m_model = scene::Model::load(m_renderer, modelPath);
        if (!m_model)
        {
            throw std::runtime_error("Failed to load model");
        }

        setupToneMapping();
        setupGraphicsPipeline();

        m_scene.cameraController().setPosition({0.0f, 1.0f, 3.0f});
        m_window.setRelativeMouseMode(true);

        // Initialize bindless state based on renderer support
        m_useBindless = m_renderer.hasBindlessSupport();
        m_renderer.setBindlessEnabled(m_useBindless);

        core::Logger::info("Bindless support: {}",
                           m_renderer.hasBindlessSupport() ? "AVAILABLE" : "NOT AVAILABLE");
        core::Logger::info("Starting with {} mode",
                           m_useBindless ? "BINDLESS" : "TRADITIONAL");
    }

    void onUpdate(float dt) override
    {
        m_ui.beginFrame();

        // Main settings window
        ImGui::Begin("Settings");

        // Bindless Toggle Section
        ImGui::SeparatorText("Rendering Mode");
        if (m_renderer.hasBindlessSupport())
        {
            if (ImGui::Checkbox("Use Bindless", &m_useBindless))
            {
                m_renderer.setBindlessEnabled(m_useBindless);
                m_stats.frameCount = 0;
                std::fill(m_stats.frameTimesMs.begin(), m_stats.frameTimesMs.end(), 0.0f);
                std::fill(m_stats.bindTimesUs.begin(), m_stats.bindTimesUs.end(), 0.0f);
                core::Logger::info("Switched to {} mode",
                                   m_useBindless ? "BINDLESS" : "TRADITIONAL");
            }
            ImGui::SameLine();
            ImGui::TextColored(
                m_useBindless ? ImVec4(0, 1, 0, 1) : ImVec4(1, 1, 0, 1),
                "%s", m_useBindless ? "BINDLESS" : "TRADITIONAL"
            );

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Press TAB to toggle quickly");
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Bindless NOT AVAILABLE");
            ImGui::TextWrapped("Enable bindless in RendererConfig at startup");
        }

        ImGui::Separator();

        // Performance Stats
        ImGui::SeparatorText("Performance");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", m_stats.avgFrameTime);
        ImGui::Text("Descriptor Binds: %d/frame", m_stats.descriptorBindCount);
        ImGui::Text("Bind Overhead: %.1f μs", m_stats.avgBindTime);

        // Update stats
        m_stats.frameTimesMs[m_stats.frameCount % 120] = dt * 1000.0f;
        m_stats.avgFrameTime = 0.0f;
        m_stats.avgBindTime = 0.0f;
        for (int i = 0; i < 120; i++)
        {
            m_stats.avgFrameTime += m_stats.frameTimesMs[i];
            m_stats.avgBindTime += m_stats.bindTimesUs[i];
        }
        m_stats.avgFrameTime /= 120.0f;
        m_stats.avgBindTime /= 120.0f;

        // Frame time graph
        ImGui::PlotLines("Frame Time (ms)",
                         m_stats.frameTimesMs.data(), 120,
                         m_stats.frameCount % 120,
                         nullptr, 0.0f, 33.0f,
                         ImVec2(0, 80));

        // Bind time graph
        ImGui::PlotLines("Bind Overhead (μs)",
                         m_stats.bindTimesUs.data(), 120,
                         m_stats.frameCount % 120,
                         nullptr, 0.0f, 500.0f,
                         ImVec2(0, 80));

        ImGui::Separator();

        // Scene Info
        ImGui::SeparatorText("Scene Info");
        if (m_model)
        {
            ImGui::Text("Nodes: %zu", m_model->nodes().size());
            ImGui::Text("Materials: %zu", m_model->materials().size());
            ImGui::Text("Textures: %zu", m_model->materials().size());
        }

        // Comparison table
        if (ImGui::CollapsingHeader("Expected Performance Gains"))
        {
            ImGui::BeginTable("comparison", 3, ImGuiTableFlags_Borders);
            ImGui::TableSetupColumn("Metric");
            ImGui::TableSetupColumn("Traditional");
            ImGui::TableSetupColumn("Bindless");
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Binds/Frame");
            ImGui::TableNextColumn();
            ImGui::Text("~%zu", m_model ? m_model->materials().size() : 0);
            ImGui::TableNextColumn();
            ImGui::Text("1");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("CPU Overhead");
            ImGui::TableNextColumn();
            ImGui::Text("~2μs/bind");
            ImGui::TableNextColumn();
            ImGui::Text("~0.1μs");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Scalability");
            ImGui::TableNextColumn();
            ImGui::Text("~100 materials");
            ImGui::TableNextColumn();
            ImGui::Text("~1M textures");

            ImGui::EndTable();
        }

        ImGui::Separator();

        // Controls help
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "TAB: Toggle bindless | ESC: Release mouse");

        // Camera controls
        if (m_input.isKeyDown(SDL_SCANCODE_ESCAPE))
        {
            m_window.setRelativeMouseMode(false);
            m_uiCapturedMouse = true;
        }
        if (m_uiCapturedMouse && !ImGui::GetIO().WantCaptureMouse &&
            m_input.isMouseButtonDown(SDL_BUTTON_LEFT))
        {
            m_window.setRelativeMouseMode(true);
            m_uiCapturedMouse = false;
        }

        ImGui::End();
        m_ui.endFrame();

        // Keyboard shortcut to toggle bindless
        if (m_input.isKeyDown(SDL_SCANCODE_TAB) && m_renderer.hasBindlessSupport())
        {
            m_useBindless = !m_useBindless;
            m_renderer.setBindlessEnabled(m_useBindless);
            m_stats.frameCount = 0;
            core::Logger::info("Quick toggle: {} mode",
                               m_useBindless ? "BINDLESS" : "TRADITIONAL");
        }

        m_stats.frameCount++;
    }

    void onEvent(const SDL_Event& event) override
    {
        m_ui.handleEvent(event);
    }

    void onRender(const RenderFrameContext& ctx) override
    {
        m_scene.update(ctx.m_deltaTime, ctx.m_extent, m_input);
        if (m_model)
        {
            m_model->updateTransforms();
        }

        // Track descriptor bind performance
        m_stats.descriptorBindCount = 0;
        auto bindStart = std::chrono::high_resolution_clock::now();
        PipelineHandle activePipeline = m_renderer.isBindlessEnabled() ? m_gfxPipelineBindless : m_gfxPipeline;

        if (m_renderer.isBindlessEnabled())
        {
            ctx.m_cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                m_renderer.pipelineLayout(activePipeline),
                0,
                m_renderer.getBindlessDescriptorSet(),
                nullptr
            );
            m_stats.descriptorBindCount = 1;
        }

        // Render model
        if (m_model)
        {
            for (const auto& node : m_model->nodes())
            {
                if (!node.meshPrimitives) continue;

                PushConstants pc{};
                pc.m_model = node.worldTransform.mat4();
                pc.m_viewProj = m_scene.camera().viewProj();

                for (const auto& prim : *node.meshPrimitives)
                {
                    const auto& mat = m_model->materials()[prim.materialIndex];

                    m_renderer.bindPipeline(ctx.m_cmd, activePipeline);

                    if (m_renderer.isBindlessEnabled())
                    {
                        pc.materialIndex = m_renderer.getTextureBindlessIndex(mat.baseColorTexture);

                        m_stats.descriptorBindCount = 1;
                    }
                    else
                    {
                        vk::DescriptorSet set = m_renderer.getTextureDescriptor(mat.baseColorTexture);
                        ctx.m_cmd.bindDescriptorSets(
                            vk::PipelineBindPoint::eGraphics,
                            m_renderer.pipelineLayout(activePipeline),
                            0, // Set 0 = traditional
                            set,
                            nullptr
                        );

                        m_stats.descriptorBindCount++;
                    }

                    m_renderer.pushConstants(ctx.m_cmd, activePipeline,
                                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                             pc);
                    m_renderer.bindMesh(ctx.m_cmd, prim.mesh);
                    m_renderer.drawMesh(ctx.m_cmd, prim.mesh);
                }
            }
        }

        // Measure bind time
        auto bindEnd = std::chrono::high_resolution_clock::now();
        auto bindDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            bindEnd - bindStart).count();
        m_stats.bindTimesUs[m_stats.frameCount % 120] = static_cast<float>(bindDuration);
    }

    void onShutdown() override
    {
        m_renderer.device().waitIdle();
        if (m_toneMapLayout)
        {
            m_renderer.device().destroyDescriptorSetLayout(m_toneMapLayout);
            m_toneMapLayout = nullptr;
        }
        if (m_descriptorPool)
        {
            m_renderer.device().destroyDescriptorPool(m_descriptorPool);
            m_descriptorPool = nullptr;
        }
        if (m_hdrSampler)
        {
            m_renderer.device().destroySampler(m_hdrSampler);
            m_hdrSampler = nullptr;
        }
        m_ui.shutdown();
    }

private:
    void setupToneMapping()
    {
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

        m_toneMapPipeline = ComputePipelineBuilder(m_renderer)
                            .setShader(toneMapShader)
                            .addDescriptorSetLayout(m_toneMapLayout)
                            .build();

        const uint32_t swapImageCount = m_renderer.getSwapchainImageCount();
        std::vector layouts(swapImageCount, m_toneMapLayout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = swapImageCount;
        allocInfo.pSetLayouts = layouts.data();

        m_toneMapSets = m_renderer.device().allocateDescriptorSets(allocInfo);

        for (uint32_t i = 0; i < swapImageCount; i++)
        {
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
            [this](vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
            {
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

    void setupGraphicsPipeline()
    {
        const auto vert = getShaderPath("gltf.vert.spv");
        const auto frag = getShaderPath("gltf.frag.spv");
        const auto vertBindless = getShaderPath("gltf_bindless.vert.spv");
        const auto fragBindless = getShaderPath("gltf_bindless.frag.spv");
        auto builder = PipelineBuilder(m_renderer)
                       .setInputTopology(vk::PrimitiveTopology::eTriangleList)
                       .setPolygonMode(vk::PolygonMode::eFill)
                       .setCullMode(vk::CullModeFlagBits::eBack)
                       .enableDepthTest(true, vk::CompareOp::eLess)
                       .setPushConstantSize(sizeof(PushConstants))
                       .setVertexInput(Vertex::getLayout())
                       .setPushConstantsShaderFlags(
                           vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);


        auto builderBindless = builder.setShaders(vertBindless, fragBindless)
                                      .useBindless();


        auto builderRegular = builder.setShaders(vert, frag)
                                     .addDescriptorSetLayout(m_renderer.getTextureDescriptorLayout());


        m_gfxPipeline = builderRegular.build();
        m_gfxPipelineBindless = builderBindless.build();
    }
};

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    GltfBindlessSample app;
    return app.run();
}
