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
    vk::DescriptorPoolSize pool_sizes[] = {
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

    vk::DescriptorPoolCreateInfo pool_info = {};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000 * 2;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    m_descriptorPool = renderer.device().createDescriptorPool(pool_info);

    // 2. Initialize ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // 3. Initialize SDL3 Backend
    ImGui_ImplSDL3_InitForVulkan(window->get());

    // 4. Initialize Vulkan Backend with Dynamic Rendering
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = renderer.instance();
    init_info.PhysicalDevice = renderer.physicalDevice();
    init_info.Device = renderer.device();
    init_info.QueueFamily = renderer.graphicsQueueFamilyIndex();
    init_info.Queue = renderer.graphicsQueue();
    init_info.PipelineCache = nullptr;
    init_info.DescriptorPool = m_descriptorPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 3; 
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;

    init_info.UseDynamicRendering = true;

    // Must match the format of the Swapchain where UI is rendered
    const VkFormat swapchainFormat = static_cast<VkFormat>(renderer.getSwapchainColorFormat());

    VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info = {};
    pipeline_rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipeline_rendering_create_info.colorAttachmentCount = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats = &swapchainFormat;

    init_info.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

    ImGui_ImplVulkan_Init(&init_info);

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
    if (m_renderer) {
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
