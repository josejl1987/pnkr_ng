#include "rhi/vulkan/vulkan_imgui.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_command_buffer.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "vulkan_cast.hpp"

#include <imgui_impl_vulkan.h>
#include <array>

#include "pnkr/core/common.hpp"

namespace pnkr::renderer::rhi::vulkan {

VulkanRHIImGui::VulkanRHIImGui(VulkanRHIDevice* device)
    : m_device(device) {
}

VulkanRHIImGui::~VulkanRHIImGui() {
    shutdown();
}

void VulkanRHIImGui::init(void* windowHandle, Format colorFormat, Format depthFormat, uint32_t framesInFlight) {
    (void)windowHandle;
    vk::Device device = m_device->device();
    vk::PhysicalDevice physicalDevice = m_device->vkPhysicalDevice();
    vk::Instance instance = m_device->instance();

    m_deferredReleases.resize(framesInFlight);

    constexpr uint32_t kDescriptorPoolSize = 4096;
    std::array<vk::DescriptorPoolSize, 11> poolSizes = {{
        { vk::DescriptorType::eSampler, kDescriptorPoolSize },
        { vk::DescriptorType::eCombinedImageSampler, kDescriptorPoolSize },
        { vk::DescriptorType::eSampledImage, kDescriptorPoolSize },
        { vk::DescriptorType::eStorageImage, kDescriptorPoolSize },
        { vk::DescriptorType::eUniformTexelBuffer, kDescriptorPoolSize },
        { vk::DescriptorType::eStorageTexelBuffer, kDescriptorPoolSize },
        { vk::DescriptorType::eUniformBuffer, kDescriptorPoolSize },
        { vk::DescriptorType::eStorageBuffer, kDescriptorPoolSize },
        { vk::DescriptorType::eUniformBufferDynamic, kDescriptorPoolSize },
        { vk::DescriptorType::eStorageBufferDynamic, kDescriptorPoolSize },
        { vk::DescriptorType::eInputAttachment, kDescriptorPoolSize }
    }};

    vk::DescriptorPoolCreateInfo poolInfo = {};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = kDescriptorPoolSize;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    m_descriptorPool = device.createDescriptorPool(poolInfo);
    m_device->trackObject(vk::ObjectType::eDescriptorPool,
                          pnkr::util::u64(static_cast<VkDescriptorPool>(m_descriptorPool)),
                          "ImGuiDescriptorPool");

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = m_device->graphicsQueueFamily();
    initInfo.Queue = m_device->graphicsQueue();
    initInfo.PipelineCache = nullptr;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = std::max(2u, framesInFlight);
    initInfo.ImageCount = std::max(2u, framesInFlight);
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;

    vk::Format vkColorFormat = VulkanUtils::toVkFormat(colorFormat);
    vk::Format vkDepthFormat = VulkanUtils::toVkFormat(depthFormat);

    VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {};
    pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = reinterpret_cast<const VkFormat*>(&vkColorFormat);
    pipelineRenderingCreateInfo.depthAttachmentFormat = static_cast<VkFormat>(vkDepthFormat);

    initInfo.PipelineRenderingCreateInfo = pipelineRenderingCreateInfo;

    ImGui_ImplVulkan_Init(&initInfo);

    {
        auto lock = m_device->acquireQueueLock();
        ImGui_ImplVulkan_CreateFontsTexture();
    }
}

void VulkanRHIImGui::shutdown() {
    if (m_descriptorPool) {
        ImGui_ImplVulkan_Shutdown();
        
        // Clear all deferred queues before destroying the pool
        m_deferredReleases.clear();

        m_device->untrackObject(pnkr::util::u64(static_cast<VkDescriptorPool>(m_descriptorPool)));
        m_device->device().destroyDescriptorPool(m_descriptorPool);
        m_descriptorPool = nullptr;
    }
}

void VulkanRHIImGui::beginFrame(uint32_t frameIndex) {
    m_currentFrameIndex = frameIndex;

    const uint32_t slot = frameIndex % static_cast<uint32_t>(m_deferredReleases.size());
    auto& queue = m_deferredReleases[slot];
    
    if (!queue.empty()) {
        /*
        for (auto ds : queue) {
           core::Logger::Render.debug("Unfreeing ImGui DS: {}", (void*)ds);
        }
        */
        m_device->device().freeDescriptorSets(m_descriptorPool, queue);
        queue.clear();
    }

    ImGui_ImplVulkan_NewFrame();
}

void VulkanRHIImGui::renderDrawData(RHICommandList* cmd, ImDrawData* drawData) {
    auto* vkCmd = rhi_cast<VulkanRHICommandBuffer>(cmd);
    ImGui_ImplVulkan_RenderDrawData(drawData, vkCmd->commandBuffer());
}

void* VulkanRHIImGui::registerTexture(void* nativeTextureView, void* nativeSampler) {
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(nativeSampler),
        static_cast<VkImageView>(nativeTextureView),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    // core::Logger::Render.debug("Allocated ImGui DS: {}", (void*)ds);
    return static_cast<void*>(ds);
}

void VulkanRHIImGui::removeTexture(void* descriptorSet) {
  auto ds = static_cast<VkDescriptorSet>(descriptorSet);
  if (ds && !m_deferredReleases.empty()) {
      const uint32_t slot = m_currentFrameIndex % static_cast<uint32_t>(m_deferredReleases.size());
      // core::Logger::Render.debug("Deferring ImGui DS release: {} to slot {} (frame: {})", (void*)ds, slot, m_currentFrameIndex);
      m_deferredReleases[slot].push_back(ds);
  }
}

}

