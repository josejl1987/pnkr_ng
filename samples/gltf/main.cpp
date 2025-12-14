#include <pnkr/engine.hpp>
#include <pnkr/core/logger.hpp>
#include <pnkr/core/timer.h>
#include <pnkr/renderer/renderer.hpp>
#include <pnkr/renderer/scene/Scene.hpp>
#include <pnkr/renderer/scene/Model.hpp>
#include <filesystem>
#include <imgui.h>

#include "pnkr/renderer/vulkan/PushConstants.h"
#include "pnkr/renderer/vulkan/pipeline/PipelineBuilder.h"
#include "pnkr/renderer/vulkan/pipeline/compute_pipeline.hpp" // Added
#include "pnkr/ui/imgui_layer.hpp"

using namespace pnkr;
using namespace pnkr::renderer;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // Boilerplate Init
    Log::init();
    Log::info("Starting GLTF Sample");

    fs::path exePath = argc > 0 ? fs::path(argv[0]).parent_path() : fs::current_path();
    fs::path assetDir = exePath / "assets";
    fs::path shaderDir = exePath / "shaders";

    platform::Window window("PNKR GLTF + Compute", 1280, 720);
    platform::Input input;
    Renderer renderer(window);

    Timer timer;
    scene::Scene scene;

    // Initialize UI Layer
    pnkr::ui::ImGuiLayer ui;
    ui.init(renderer, &window);

    // Load Model
    fs::path modelPath = assetDir / "Duck.glb";
    if (!fs::exists(modelPath)) {
        Log::error("Model not found at: {}", modelPath.string());
        return 1;
    }

    auto model = scene::Model::load(renderer, modelPath);
    if (!model) return 1;

    // --- COMPUTE SHADER SETUP ---

    // 1. Create a Local Descriptor Pool for the sample
    vk::DescriptorPoolSize poolSizes[] = {
        { vk::DescriptorType::eCombinedImageSampler, 10 },
        { vk::DescriptorType::eStorageImage, 10 }
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 10;
    // Create the pool
    vk::DescriptorPool descriptorPool = renderer.device().createDescriptorPool(poolInfo);

    // 2. Create a Sampler for reading the HDR offscreen image
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    // Create the sampler
    vk::Sampler hdrSampler = renderer.device().createSampler(samplerInfo);


    // 3. Setup Descriptor Layout for Tone Mapping
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
    vk::DescriptorSetLayout toneMapLayout = renderer.device().createDescriptorSetLayout(layoutInfo);

    // 4. Build Compute Pipeline
    auto toneMapPipeline = pnkr::renderer::ComputePipelineBuilder(renderer)
        .setShader(shaderDir / "tonemap.comp.spv")
        .addDescriptorSetLayout(toneMapLayout)
        .build();

    // 5. Allocate Descriptor Sets (One per Swapchain Image)
    uint32_t swapImageCount = renderer.getSwapchainImageCount();
    std::vector<vk::DescriptorSetLayout> layouts(swapImageCount, toneMapLayout);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = descriptorPool; // FIX: Use the local pool, not commandPool
    allocInfo.descriptorSetCount = swapImageCount;
    allocInfo.pSetLayouts = layouts.data();

    std::vector<vk::DescriptorSet> toneMapSets = renderer.device().allocateDescriptorSets(allocInfo);

    // 6. Update Descriptors
    for (uint32_t i = 0; i < swapImageCount; i++) {
        vk::DescriptorImageInfo hdrInfo{};
        hdrInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        // The Renderer exposes getOffscreenTexture() which returns the VulkanImage
        hdrInfo.imageView = renderer.getOffscreenTexture().view();
        hdrInfo.sampler = hdrSampler; // FIX: Use the local sampler

        vk::DescriptorImageInfo swapInfo{};
        swapInfo.imageLayout = vk::ImageLayout::eGeneral;
        swapInfo.imageView = renderer.getSwapchainImageView(i);

        vk::WriteDescriptorSet writes[2]{};
        writes[0].dstSet = toneMapSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &hdrInfo;

        writes[1].dstSet = toneMapSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageImage;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &swapInfo;

        renderer.device().updateDescriptorSets(2, writes, 0, nullptr);
    }

    // 7. Register Callback
    renderer.setPostProcessCallback([&](vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent) {
        renderer.bindPipeline(cmd, *toneMapPipeline);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            toneMapPipeline->layout(),
            0,
            toneMapSets[imageIndex],
            nullptr
        );

        uint32_t groupX = (extent.width + 15) / 16;
        uint32_t groupY = (extent.height + 15) / 16;
        renderer.dispatch(cmd, groupX, groupY, 1);
    });

    // --- GRAPHICS PIPELINE SETUP ---

    auto pipeline = PipelineBuilder(renderer)
        .setShaders(shaderDir / "gltf.vert.spv", shaderDir / "gltf.frag.spv")
        .setInputTopology(vk::PrimitiveTopology::eTriangleList)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .enableDepthTest(true, vk::CompareOp::eLess)
        .addDescriptorSetLayout(renderer.getTextureDescriptorLayout())
        .setPushConstantSize(sizeof(PushConstants))
        .setPushConstantsShaderFlags(vk::ShaderStageFlagBits::eVertex)
        .build();

    // Setup Camera
    scene.cameraController().setPosition({0.0f, 1.0f, 3.0f});

    window.setRelativeMouseMode(true);
    bool uiCapturedMouse = false;

    // Rendering Logic
    renderer.setRecordFunc([&](const RenderFrameContext& ctx) {
        scene.update(ctx.m_deltaTime, ctx.m_extent, input);
        model->updateTransforms();

        for (const auto& node : model->nodes()) {
            if (!node.meshPrimitives) continue;
            PushConstants pc{};
            pc.m_model = node.worldTransform.mat4();
            pc.m_viewProj = scene.camera().viewProj();

            for (const auto& prim : *node.meshPrimitives) {
                const auto& mat = model->materials()[prim.materialIndex];
                renderer.bindPipeline(ctx.m_cmd, pipeline);
                vk::DescriptorSet set = renderer.getTextureDescriptor(mat.baseColorTexture);
                ctx.m_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, renderer.pipelineLayout(pipeline), 0, set, nullptr);
                renderer.pushConstants(ctx.m_cmd, pipeline, vk::ShaderStageFlagBits::eVertex, pc);
                renderer.bindMesh(ctx.m_cmd, prim.mesh);
                renderer.drawMesh(ctx.m_cmd, prim.mesh);
            }
        }
    });

    while (window.isRunning()) {
        window.processEvents(&input, [&](const SDL_Event& e) {
            ui.handleEvent(e);
            // Return true if handled? Current signature is void in prev plan,
            // if you updated it to bool as suggested in Code Review, return ImGui::GetIO().WantCaptureMouse
        });

        ui.beginFrame();
        ImGui::Begin("Settings");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        if (input.isKeyDown(SDL_SCANCODE_ESCAPE)) {
            window.setRelativeMouseMode(false);
            uiCapturedMouse = true;
        }
        if (uiCapturedMouse && !ImGui::GetIO().WantCaptureMouse && input.isMouseButtonDown(SDL_BUTTON_LEFT)) {
            window.setRelativeMouseMode(true);
            uiCapturedMouse = false;
        }

        ImGui::End();
        ui.endFrame();

        renderer.beginFrame(timer.deltaTime());
        renderer.drawFrame();
        renderer.endFrame();
    }

    // Cleanup
    renderer.device().waitIdle();
    renderer.device().destroyDescriptorSetLayout(toneMapLayout);
    renderer.device().destroyDescriptorPool(descriptorPool);
    renderer.device().destroySampler(hdrSampler);
    ui.shutdown();

    return 0;
}