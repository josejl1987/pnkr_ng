//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/image/vulkan_sampler.hpp"

namespace pnkr::renderer {

    VulkanSampler::VulkanSampler(vk::Device device,
                                 vk::Filter magFilter,
                                 vk::Filter minFilter,
                                 vk::SamplerAddressMode addressMode)
      : m_device(device)
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = magFilter;
        samplerInfo.minFilter = minFilter;
        samplerInfo.addressModeU = addressMode;
        samplerInfo.addressModeV = addressMode;
        samplerInfo.addressModeW = addressMode;
        samplerInfo.anisotropyEnable = false;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        m_sampler = device.createSampler(samplerInfo);
    }

    VulkanSampler::~VulkanSampler() {
        destroy();
    }

    VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
      : m_device(other.m_device)
      , m_sampler(other.m_sampler)
    {
        other.m_sampler = nullptr;
    }

    VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept {
        if (this != &other) {
            destroy();
            m_device = other.m_device;
            m_sampler = other.m_sampler;
            other.m_sampler = nullptr;
        }
        return *this;
    }

    void VulkanSampler::destroy() noexcept {
        if (m_sampler) {
            m_device.destroySampler(m_sampler);
            m_sampler = nullptr;
        }
    }

} // namespace pnkr::renderer
