#include "pnkr/ui/imgui_layer.hpp"

// External libs
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <cpptrace/cpptrace.hpp>

// Engine RHI Interfaces
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/platform/window.hpp"

// RHI Vulkan Implementation (Required for ImGui_ImplVulkan)
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_command_buffer.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"

namespace pnkr::ui {

// Helper to cast the opaque pointer back to the Vulkan handle
static vk::DescriptorPool& GetPool(void*& handle) {
    return *reinterpret_cast<vk::DescriptorPool*>(&handle);
}

void ImGuiLayer::init(pnkr::renderer::RHIRenderer* renderer, pnkr::platform::Window* window) {
    m_renderer = renderer;

    // 1. Resolve RHI Device to Vulkan Device
    // We cannot avoid this because ImGui_ImplVulkan needs raw handles.
    auto* vkDeviceWrapper = dynamic_cast<renderer::rhi::vulkan::VulkanRHIDevice*>(m_renderer->device());
    if (!vkDeviceWrapper) {
        throw cpptrace::runtime_error("[ImGuiLayer] RHI Backend is not Vulkan! ImGui_ImplVulkan requires Vulkan.");
    }
    
    vk::Device device = vkDeviceWrapper->device();
    vk::PhysicalDevice physicalDevice = vkDeviceWrapper->vkPhysicalDevice();
    vk::Instance instance = vkDeviceWrapper->instance();

    // 2. Create Descriptor Pool
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
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    // Store in our void* handle
    vk::DescriptorPool pool = device.createDescriptorPool(poolInfo);
    m_descriptorPool = *reinterpret_cast<void**>(&pool);

    // 3. Initialize Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // 4. Init SDL3
    ImGui_ImplSDL3_InitForVulkan(window->get());

    // 5. Init Vulkan Backend
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = vkDeviceWrapper->graphicsQueueFamily();
    initInfo.Queue = vkDeviceWrapper->graphicsQueue();
    initInfo.PipelineCache = nullptr;
    initInfo.DescriptorPool = GetPool(m_descriptorPool);
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 3;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Dynamic Rendering Setup
    initInfo.UseDynamicRendering = true;
    
    // Use RHI Utils to convert formats
    const auto colorFormat = renderer::rhi::vulkan::VulkanUtils::toVkFormat(m_renderer->getDrawColorFormat());
    const auto depthFormat = renderer::rhi::vulkan::VulkanUtils::toVkFormat(m_renderer->getDrawDepthFormat());

    VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = reinterpret_cast<const VkFormat*>(&colorFormat);
    pipelineRenderingCreateInfo.depthAttachmentFormat = static_cast<VkFormat>(depthFormat);

    initInfo.PipelineRenderingCreateInfo = pipelineRenderingCreateInfo;

    ImGui_ImplVulkan_Init(&initInfo);

    m_initialized = true;

    // 6. Upload Fonts (ImGui backend handles its own command buffer)
    ImGui_ImplVulkan_CreateFontsTexture();
    m_renderer->device()->waitIdle();


}

void ImGuiLayer::shutdown() {
    if (m_renderer && m_initialized) {
        m_renderer->device()->waitIdle();
        
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        // Destroy pool using the raw device wrapper
        auto* vkDeviceWrapper = dynamic_cast<renderer::rhi::vulkan::VulkanRHIDevice*>(m_renderer->device());
        if (vkDeviceWrapper) {
            vkDeviceWrapper->device().destroyDescriptorPool(GetPool(m_descriptorPool));
        }
        
        m_renderer = nullptr;
        m_initialized = false;
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
    ImGui::Render();
}

void ImGuiLayer::render(pnkr::renderer::rhi::RHICommandBuffer* cmd) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData) {
        // Cast the abstract RHI buffer to the concrete Vulkan implementation
        auto* vkCmd = dynamic_cast<renderer::rhi::vulkan::VulkanRHICommandBuffer*>(cmd);
        if (vkCmd) {
            ImGui_ImplVulkan_RenderDrawData(drawData, vkCmd->commandBuffer());
        }
    }
}

} // namespace pnkr::ui
