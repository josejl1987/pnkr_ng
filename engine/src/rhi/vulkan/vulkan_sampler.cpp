#include "pnkr/rhi/vulkan/vulkan_sampler.hpp"

#include "pnkr/rhi/vulkan/vulkan_device.hpp"
#include "pnkr/rhi/vulkan/vulkan_utils.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    VulkanRHISampler::VulkanRHISampler(VulkanRHIDevice* device,
                                       Filter minFilter,
                                       Filter magFilter,
                                       SamplerAddressMode addressMode,
                                       CompareOp compareOp)
        : m_device(device)
    {
        m_isShadowSampler = (compareOp != CompareOp::None);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = VulkanUtils::toVkFilter(magFilter);
        samplerInfo.minFilter = VulkanUtils::toVkFilter(minFilter);
        samplerInfo.addressModeU = VulkanUtils::toVkAddressMode(addressMode);
        samplerInfo.addressModeV = VulkanUtils::toVkAddressMode(addressMode);
        samplerInfo.addressModeW = VulkanUtils::toVkAddressMode(addressMode);
        
        // Anisotropic filtering
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0F;
        
        // Border color
        if (addressMode == SamplerAddressMode::ClampToBorder && compareOp != CompareOp::None) {
            samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        } else {
            samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        }
        
        // Unnormalized coordinates
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        
        // Comparison
        samplerInfo.compareEnable = m_isShadowSampler ? VK_TRUE : VK_FALSE;
        samplerInfo.compareOp = VulkanUtils::toVkCompareOp(compareOp);
        
        // Mipmapping
        samplerInfo.mipmapMode = minFilter == Filter::Linear 
            ? vk::SamplerMipmapMode::eLinear 
            : vk::SamplerMipmapMode::eNearest;
        samplerInfo.mipLodBias = 0.0F;
        samplerInfo.minLod = 0.0F;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        m_sampler = m_device->device().createSampler(samplerInfo);
    }

    VulkanRHISampler::~VulkanRHISampler()
    {
        if (m_bindlessHandle.isValid()) {
            if (m_isShadowSampler) {
                m_device->releaseBindlessShadowSampler(m_bindlessHandle);
            } else {
                m_device->releaseBindlessSampler(m_bindlessHandle);
            }
        }

        if (m_sampler) {
            m_device->device().destroySampler(m_sampler);
        }
    }

} // namespace pnkr::renderer::rhi::vulkan
