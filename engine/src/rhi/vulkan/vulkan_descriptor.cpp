#include "pnkr/rhi/vulkan/vulkan_descriptor.hpp"

#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_sampler.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHIDescriptorSetLayout::VulkanRHIDescriptorSetLayout(
        VulkanRHIDevice* device,
        vk::DescriptorSetLayout layout,
        const DescriptorSetLayout& desc,
        bool ownsLayout)
        : m_device(device)
        , m_layout(layout)
        , m_desc(desc)
        , m_ownsLayout(ownsLayout)
    {
        for (const auto& binding : desc.bindings)
        {
            m_bindingTypes.emplace(binding.binding, binding.type);
        }
    }

    VulkanRHIDescriptorSetLayout::~VulkanRHIDescriptorSetLayout()
    {
        if ((m_device != nullptr) && m_layout && m_ownsLayout)
        {
            m_device->device().destroyDescriptorSetLayout(m_layout);
            m_layout = nullptr;
        }
    }

    const DescriptorSetLayout& VulkanRHIDescriptorSetLayout::description() const
    {
        return m_desc;
    }

    DescriptorType VulkanRHIDescriptorSetLayout::descriptorType(uint32_t binding) const
    {
        auto it = m_bindingTypes.find(binding);
        if (it == m_bindingTypes.end())
        {
            return DescriptorType::UniformBuffer;
        }
        return it->second;
    }

    VulkanRHIDescriptorSet::VulkanRHIDescriptorSet(
        VulkanRHIDevice* device,
        VulkanRHIDescriptorSetLayout* layout,
        vk::DescriptorSet set)
        : m_device(device)
        , m_layout(layout)
        , m_set(set)
    {
    }

    void VulkanRHIDescriptorSet::updateBuffer(uint32_t binding,
                                              RHIBuffer* buffer,
                                              uint64_t offset,
                                              uint64_t range)
    {
        if (buffer == nullptr)
        {
            core::Logger::error("updateBuffer: buffer is null");
            return;
        }

        auto type = m_layout->descriptorType(binding);
        if (type != DescriptorType::UniformBuffer &&
            type != DescriptorType::StorageBuffer &&
            type != DescriptorType::UniformBufferDynamic &&
            type != DescriptorType::StorageBufferDynamic)
        {
            core::Logger::error("updateBuffer: binding {} is not a buffer descriptor", binding);
            return;
        }

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vk::Buffer(getVkBufferFromRHI(buffer->nativeHandle()));
        bufferInfo.offset = offset;
        bufferInfo.range = range;

        vk::WriteDescriptorSet write{};
        write.dstSet = m_set;
        write.dstBinding = binding;
        write.descriptorType = VulkanUtils::toVkDescriptorType(type);
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        m_device->device().updateDescriptorSets(write, nullptr);
    }

    void VulkanRHIDescriptorSet::updateTexture(uint32_t binding,
                                               RHITexture* texture,
                                               RHISampler* sampler)
    {
        auto type = m_layout->descriptorType(binding);
        if (type != DescriptorType::CombinedImageSampler &&
            type != DescriptorType::Sampler &&
            type != DescriptorType::SampledImage &&
            type != DescriptorType::StorageImage)
        {
            core::Logger::error("updateTexture: binding {} is not an image descriptor", binding);
            return;
        }
        if (type != DescriptorType::Sampler && texture == nullptr)
        {
            core::Logger::error("updateTexture: texture is null");
            return;
        }
        if ((type == DescriptorType::CombinedImageSampler || type == DescriptorType::Sampler) &&
            (sampler == nullptr))
        {
            core::Logger::error("updateTexture: sampler is null");
            return;
        }

        vk::DescriptorImageInfo imageInfo{};
        if (type == DescriptorType::Sampler) {
            imageInfo.sampler = vk::Sampler(static_cast<VkSampler>(sampler->nativeHandle()));
        } else {
            imageInfo.imageView = vk::ImageView(static_cast<VkImageView>(texture->nativeView()));
            if (type == DescriptorType::StorageImage)
            {
                imageInfo.imageLayout = vk::ImageLayout::eGeneral;
                imageInfo.sampler = nullptr;
            }
            else if (type == DescriptorType::SampledImage)
            {
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfo.sampler = nullptr;
            }
            else
            {
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfo.sampler = vk::Sampler(static_cast<VkSampler>(sampler->nativeHandle()));
            }
        }

        vk::WriteDescriptorSet write{};
        write.dstSet = m_set;
        write.dstBinding = binding;
        write.descriptorType = VulkanUtils::toVkDescriptorType(type);
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        m_device->device().updateDescriptorSets(write, nullptr);
    }
} // namespace pnkr::renderer::rhi::vulkan
