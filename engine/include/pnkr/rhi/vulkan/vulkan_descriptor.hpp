#pragma once

#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHIDescriptorSetLayout : public RHIDescriptorSetLayout
    {
    public:
        VulkanRHIDescriptorSetLayout(VulkanRHIDevice* device,
                                     vk::DescriptorSetLayout layout,
                                     const DescriptorSetLayout& desc,
                                     bool ownsLayout = true);
        ~VulkanRHIDescriptorSetLayout() override;

        void* nativeHandle() const override
        {
            return static_cast<VkDescriptorSetLayout>(m_layout);
        }

        vk::DescriptorSetLayout layout() const { return m_layout; }
        DescriptorType descriptorType(uint32_t binding) const;
        const DescriptorSetLayout& description() const override;

    private:
        VulkanRHIDevice* m_device = nullptr;
        vk::DescriptorSetLayout m_layout{};
        DescriptorSetLayout m_desc;
        std::unordered_map<uint32_t, DescriptorType> m_bindingTypes;
        bool m_ownsLayout = true;
    };

    class VulkanRHIDescriptorSet : public RHIDescriptorSet
    {
    public:
        VulkanRHIDescriptorSet(VulkanRHIDevice* device,
                               VulkanRHIDescriptorSetLayout* layout,
                               vk::DescriptorSet set);

        void updateBuffer(uint32_t binding,
                          RHIBuffer* buffer,
                          uint64_t offset,
                          uint64_t range) override;
        void updateTexture(uint32_t binding,
                           RHITexture* texture,
                           RHISampler* sampler) override;

        void* nativeHandle() const override
        {
            return static_cast<VkDescriptorSet>(m_set);
        }

    private:
        VulkanRHIDevice* m_device = nullptr;
        VulkanRHIDescriptorSetLayout* m_layout = nullptr;
        vk::DescriptorSet m_set{};
    };
} // namespace pnkr::renderer::rhi::vulkan
