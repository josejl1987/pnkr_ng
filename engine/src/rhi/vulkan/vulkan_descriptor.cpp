#include "rhi/vulkan/vulkan_descriptor.hpp"

#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_sampler.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/common.hpp"
#include "rhi/vulkan/DescriptorUpdater.hpp"

namespace pnkr::renderer::rhi::vulkan
{
VulkanRHIDescriptorSetLayout::VulkanRHIDescriptorSetLayout(
    VulkanRHIDevice *device, vk::DescriptorSetLayout layout,
    const DescriptorSetLayout &desc, bool ownsLayout)
    : VulkanRHIResourceBase(device), m_desc(desc),
      m_vkDevice((device != nullptr) ? device->device() : vk::Device{}),
      m_ownsLayout(ownsLayout) {
  m_handle = layout;
  if (m_device != nullptr && m_ownsLayout) {
    m_device->trackObject(
        vk::ObjectType::eDescriptorSetLayout,
        pnkr::util::u64(static_cast<VkDescriptorSetLayout>(m_handle)),
        "DescriptorSetLayout");
  }
  for (const auto &binding : desc.bindings) {
    m_bindingTypes.emplace(binding.binding, binding.type);
  }
}

    VulkanRHIDescriptorSetLayout::VulkanRHIDescriptorSetLayout(
        vk::Device device,
        vk::DescriptorSetLayout layout,
        const DescriptorSetLayout& desc,
        bool ownsLayout)
        : VulkanRHIResourceBase(nullptr)
        , m_desc(desc)
        , m_vkDevice(device)
        , m_ownsLayout(ownsLayout)
    {
        m_handle = layout;
        for (const auto& binding : desc.bindings)
        {
            m_bindingTypes.emplace(binding.binding, binding.type);
        }
    }

    VulkanRHIDescriptorSetLayout::~VulkanRHIDescriptorSetLayout()
    {
        if (m_vkDevice && m_handle && m_ownsLayout)
        {
            if (m_device != nullptr) {
                m_device->untrackObject(
                    pnkr::util::u64(static_cast<VkDescriptorSetLayout>(m_handle)));
            }
            m_vkDevice.destroyDescriptorSetLayout(m_handle);
            m_handle = nullptr;
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
        VulkanRHIDevice *device, VulkanRHIDescriptorSetLayout *layout,
        vk::DescriptorSet set)
        : VulkanRHIResourceBase(device), m_layout(layout),
          m_vkDevice((device != nullptr) ? device->device() : vk::Device{}) {
      m_handle = set;
      if (m_device != nullptr) {
        m_device->trackObject(
            vk::ObjectType::eDescriptorSet,
            pnkr::util::u64(static_cast<VkDescriptorSet>(m_handle)),
            "DescriptorSet");
      }
    }

    VulkanRHIDescriptorSet::VulkanRHIDescriptorSet(
        vk::Device device,
        VulkanRHIDescriptorSetLayout* layout,
        vk::DescriptorSet set)
        : VulkanRHIResourceBase(nullptr)
        , m_layout(layout)
        , m_vkDevice(device)
    {
        m_handle = set;
    }

    VulkanRHIDescriptorSet::~VulkanRHIDescriptorSet()
    {
        if (m_device != nullptr && m_handle) {
            m_device->untrackObject(
                pnkr::util::u64(static_cast<VkDescriptorSet>(m_handle)));
        }
    }

    void VulkanRHIDescriptorSet::updateBuffer(uint32_t binding,
                                              RHIBuffer* buffer,
                                              uint64_t offset,
                                              uint64_t range)
    {
        if (buffer == nullptr)
        {
            core::Logger::RHI.error("updateBuffer: buffer is null");
            return;
        }

        auto type = m_layout->descriptorType(binding);
        if (type != DescriptorType::UniformBuffer &&
            type != DescriptorType::StorageBuffer &&
            type != DescriptorType::UniformBufferDynamic &&
            type != DescriptorType::StorageBufferDynamic)
        {
            core::Logger::RHI.error("updateBuffer: binding {} is not a buffer descriptor", binding);
            return;
        }

        DescriptorUpdater(m_vkDevice, m_handle)
            .writeBuffer(binding, VulkanUtils::toVkDescriptorType(type), vk::Buffer(static_cast<VkBuffer>(buffer->nativeHandle())), offset, range)
            .commit();
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
            core::Logger::RHI.error("updateTexture: binding {} is not an image descriptor", binding);
            return;
        }
        if (type != DescriptorType::Sampler && texture == nullptr)
        {
            core::Logger::RHI.error("updateTexture: texture is null");
            return;
        }
        if ((type == DescriptorType::CombinedImageSampler || type == DescriptorType::Sampler) &&
            (sampler == nullptr))
        {
            core::Logger::RHI.error("updateTexture: sampler is null");
            return;
        }

        DescriptorUpdater updater(m_vkDevice, m_handle);

        if (type == DescriptorType::Sampler) {
            updater.writeImage(binding, VulkanUtils::toVkDescriptorType(type), nullptr, vk::ImageLayout::eUndefined, vk::Sampler(static_cast<VkSampler>(sampler->nativeHandle())));
        } else {
          auto view =
              vk::ImageView(static_cast<VkImageView>(texture->nativeView()));
          if (type == DescriptorType::StorageImage) {
            updater.writeImage(binding, VulkanUtils::toVkDescriptorType(type),
                               view, vk::ImageLayout::eGeneral);
          } else if (type == DescriptorType::SampledImage) {
            updater.writeImage(binding, VulkanUtils::toVkDescriptorType(type),
                               view, vk::ImageLayout::eShaderReadOnlyOptimal);
          } else {
            updater.writeImage(
                binding, VulkanUtils::toVkDescriptorType(type), view,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::Sampler(static_cast<VkSampler>(sampler->nativeHandle())));
          }
        }

        updater.commit();
    }
}

