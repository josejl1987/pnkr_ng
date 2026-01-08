#pragma once

#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include "VulkanRHIResourceBase.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHIDescriptorSetLayout : public VulkanRHIResourceBase<vk::DescriptorSetLayout, RHIDescriptorSetLayout>
    {
    public:
        VulkanRHIDescriptorSetLayout(VulkanRHIDevice* device,
                                     vk::DescriptorSetLayout layout,
                                     const DescriptorSetLayout& desc,
                                     bool ownsLayout = true);
        VulkanRHIDescriptorSetLayout(vk::Device device,
                                     vk::DescriptorSetLayout layout,
                                     const DescriptorSetLayout& desc,
                                     bool ownsLayout = true);
        ~VulkanRHIDescriptorSetLayout() override;

        vk::DescriptorSetLayout layout() const { return m_handle; }
        DescriptorType descriptorType(uint32_t binding) const;
        const DescriptorSetLayout& description() const override;

    private:
        DescriptorSetLayout m_desc;
        std::unordered_map<uint32_t, DescriptorType> m_bindingTypes;
        vk::Device m_vkDevice;
        bool m_ownsLayout = true;
    };

    class VulkanRHIDescriptorSet : public VulkanRHIResourceBase<vk::DescriptorSet, RHIDescriptorSet>
    {
    public:
        VulkanRHIDescriptorSet(VulkanRHIDevice* device,
                               VulkanRHIDescriptorSetLayout* layout,
                               vk::DescriptorSet set);
        VulkanRHIDescriptorSet(vk::Device device,
                               VulkanRHIDescriptorSetLayout* layout,
                               vk::DescriptorSet set);
        ~VulkanRHIDescriptorSet() override;

        void updateBuffer(uint32_t binding,
                          RHIBuffer* buffer,
                          uint64_t offset,
                          uint64_t range) override;
        void updateTexture(uint32_t binding,
                           RHITexture* texture,
                           RHISampler* sampler) override;

    private:
        VulkanRHIDescriptorSetLayout* m_layout = nullptr;
        vk::Device m_vkDevice;
    };
}
