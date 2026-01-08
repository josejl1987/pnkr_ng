#pragma once
#include "pnkr/rhi/rhi_imgui.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan {

    class VulkanRHIDevice;

    class VulkanRHIImGui final : public RHIImGui {
    public:
        explicit VulkanRHIImGui(VulkanRHIDevice* device);
        ~VulkanRHIImGui() override;

        void init(void* windowHandle, Format colorFormat, Format depthFormat, uint32_t framesInFlight) override;
        void shutdown() override;

        void beginFrame(uint32_t frameIndex) override;
        void renderDrawData(RHICommandList* cmd, ImDrawData* drawData) override;

        void* registerTexture(void* nativeTextureView, void* nativeSampler) override;
        void removeTexture(void* descriptorSet) override;

    private:
        VulkanRHIDevice* m_device;
        vk::DescriptorPool m_descriptorPool;
        
        std::vector<std::vector<vk::DescriptorSet>> m_deferredReleases;
        uint32_t m_currentFrameIndex = 0;
    };

}
