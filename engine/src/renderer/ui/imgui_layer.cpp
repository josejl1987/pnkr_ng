//
// Created by Jose on 12/14/2025.
//

#include "pnkr/ui/imgui_layer.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>

#include "pnkr/engine.hpp"

namespace pnkr::ui {

void ImGuiLayer::init(pnkr::renderer::Renderer& renderer, pnkr::platform::Window* window) {
    m_renderer = &renderer;

    // 1. Create Descriptor Pool for ImGui
    vk::DescriptorPoolSize poolSizes[] = {
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eUniformTexelBuffer, 1000 },
        { vk::DescriptorType::eStorageTexelBuffer, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    };

    vk::DescriptorPoolCreateInfo poolInfo = {};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000 * 2;
    poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    m_descriptorPool = renderer.device().createDescriptorPool(poolInfo);

    // 2. Initialize ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // 3. Initialize SDL3 Backend
    ImGui_ImplSDL3_InitForVulkan(window->get());

    // 4. Initialize Vulkan Backend with Dynamic Rendering
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = renderer.instance();
    initInfo.PhysicalDevice = renderer.physicalDevice();
    initInfo.Device = renderer.device();
    initInfo.QueueFamily = renderer.graphicsQueueFamilyIndex();
    initInfo.Queue = renderer.graphicsQueue();
    initInfo.PipelineCache = nullptr;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 3; 
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    initInfo.UseDynamicRendering = true;

    // Must match the format of the Swapchain where UI is rendered
    const auto swapchainFormat = static_cast<VkFormat>(renderer.getSwapchainColorFormat());

    VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

    initInfo.PipelineRenderingCreateInfo = pipelineRenderingCreateInfo;

    ImGui_ImplVulkan_Init(&initInfo);

    // 5. Upload Fonts using a temporary command buffer
    vk::CommandPool cmdPool = renderer.commandPool();
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = cmdPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer cmd = renderer.device().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    ImGui_ImplVulkan_CreateFontsTexture();

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    renderer.graphicsQueue().submit(submitInfo, nullptr);
    renderer.device().waitIdle(); 

    renderer.device().freeCommandBuffers(cmdPool, cmd);
}

void ImGuiLayer::shutdown() {
    if (m_renderer != nullptr) {
        m_renderer->device().waitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_renderer->device().destroyDescriptorPool(m_descriptorPool);
        m_renderer = nullptr;
    }
}

void ImGuiLayer::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    // Generate draw data. Renderer::drawFrame() picks this up.
    ImGui::Render();
}

} // namespace pnkr::ui
