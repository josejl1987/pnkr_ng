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
#include "pnkr/rhi/vulkan/vulkan_texture.hpp"

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
    constexpr uint32_t kDescriptorPoolSize = 4096;
    vk::DescriptorPoolSize poolSizes[] = {
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
    };

    vk::DescriptorPoolCreateInfo poolInfo = {};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = kDescriptorPoolSize;
    poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    // Store in our void* handle
    vk::DescriptorPool pool = device.createDescriptorPool(poolInfo);
    m_descriptorPool = *reinterpret_cast<void**>(&pool);

    // Create a dedicated static sampler for UI images (Linear, ClampToEdge)
    // This ensures depth textures are read as data, not compared.
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.minLod = -1000;
    samplerInfo.maxLod = 1000;
    samplerInfo.maxAnisotropy = 1.0f;
    
    vk::Sampler sampler = device.createSampler(samplerInfo);
    m_uiSampler = *reinterpret_cast<void**>(&sampler);

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

        auto* vkDeviceWrapper = dynamic_cast<renderer::rhi::vulkan::VulkanRHIDevice*>(m_renderer->device());
        if (vkDeviceWrapper)
        {
            for (const auto& entry : m_textureCache)
            {
                VkDescriptorSet ds = (VkDescriptorSet)entry.second.id;
                vkDeviceWrapper->device().freeDescriptorSets(GetPool(m_descriptorPool), {ds});
            }
        }
        m_textureCache.clear();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        // Destroy pool using the raw device wrapper
        if (vkDeviceWrapper) {
            if (m_uiSampler) {
                vkDeviceWrapper->device().destroySampler(*reinterpret_cast<vk::Sampler*>(&m_uiSampler));
            }
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
    garbageCollectTextureCache();
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

ImTextureID ImGuiLayer::getTextureID(TextureHandle handle) {
    if (handle == INVALID_TEXTURE_HANDLE) return -1;

    // 1. Check Cache
    auto it = m_textureCache.find(handle.id);
    if (it != m_textureCache.end()) {
        auto* tex = m_renderer->getTexture(handle);
        if (!tex) {
            releaseTexture(handle);
            return -1;
        }

        auto* vkTex = dynamic_cast<renderer::rhi::vulkan::VulkanRHITexture*>(tex);
        if (!vkTex) {
            releaseTexture(handle);
            return -1;
        }

        VkImageView imageView = (VkImageView)vkTex->nativeView();
        uint64_t viewId = reinterpret_cast<uint64_t>(imageView);
        if (it->second.view == viewId) {
            return it->second.id;
        }

        releaseTexture(handle);
        it = m_textureCache.end();
    }

    // 2. Get underlying Vulkan Image View
    auto* tex = m_renderer->getTexture(handle);
    if (!tex) return -1;

    auto* vkTex = dynamic_cast<renderer::rhi::vulkan::VulkanRHITexture*>(tex);
    if (!vkTex) return -1;

    VkImageView imageView = (VkImageView)vkTex->nativeView();
    VkSampler sampler = *reinterpret_cast<VkSampler*>(&m_uiSampler);

    // 3. Register with ImGui
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        sampler, 
        imageView, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 4. Cache and Return
    CachedTexture cached{};
    cached.id = (ImTextureID)ds;
    cached.view = reinterpret_cast<uint64_t>(imageView);
    m_textureCache[handle.id] = cached;
    return (ImTextureID)ds;
}

void ImGuiLayer::releaseTexture(TextureHandle handle)
{
    if (!m_initialized || !m_renderer || handle == INVALID_TEXTURE_HANDLE) return;

    auto it = m_textureCache.find(handle.id);
    if (it == m_textureCache.end()) return;

    auto* vkDeviceWrapper = dynamic_cast<renderer::rhi::vulkan::VulkanRHIDevice*>(m_renderer->device());
    if (vkDeviceWrapper)
    {
        VkDescriptorSet ds = (VkDescriptorSet)it->second.id;
        vkDeviceWrapper->device().freeDescriptorSets(GetPool(m_descriptorPool), {ds});
    }
    m_textureCache.erase(it);
}

void ImGuiLayer::garbageCollectTextureCache()
{
    if (!m_initialized || !m_renderer) return;

    auto* vkDeviceWrapper = dynamic_cast<renderer::rhi::vulkan::VulkanRHIDevice*>(m_renderer->device());
    if (!vkDeviceWrapper) return;

    for (auto it = m_textureCache.begin(); it != m_textureCache.end(); )
    {
        TextureHandle handle{it->first};
        auto* tex = m_renderer->getTexture(handle);
        bool drop = false;
        if (!tex)
        {
            drop = true;
        }
        else
        {
            auto* vkTex = dynamic_cast<renderer::rhi::vulkan::VulkanRHITexture*>(tex);
            if (!vkTex)
            {
                drop = true;
            }
            else
            {
                VkImageView imageView = (VkImageView)vkTex->nativeView();
                uint64_t viewId = reinterpret_cast<uint64_t>(imageView);
                drop = (viewId != it->second.view);
            }
        }

        if (drop)
        {
            VkDescriptorSet ds = (VkDescriptorSet)it->second.id;
            vkDeviceWrapper->device().freeDescriptorSets(GetPool(m_descriptorPool), {ds});
            it = m_textureCache.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

} // namespace pnkr::ui
