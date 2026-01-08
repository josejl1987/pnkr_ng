#include "rhi/vulkan/vulkan_sampler.hpp"

#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/core/common.hpp"

namespace pnkr::renderer::rhi::vulkan
{
VulkanRHISampler::VulkanRHISampler(VulkanRHIDevice *device, Filter minFilter,
                                   Filter magFilter,
                                   SamplerAddressMode addressMode,
                                   CompareOp compareOp)
    : VulkanRHIResourceBase(device),
      m_isShadowSampler(compareOp != CompareOp::None) {

  auto samplerInfoBuilder =
      VkBuilder<vk::SamplerCreateInfo>{}
          .set(&vk::SamplerCreateInfo::magFilter,
               VulkanUtils::toVkFilter(magFilter))
          .set(&vk::SamplerCreateInfo::minFilter,
               VulkanUtils::toVkFilter(minFilter))
          .set(&vk::SamplerCreateInfo::addressModeU,
               VulkanUtils::toVkAddressMode(addressMode))
          .set(&vk::SamplerCreateInfo::addressModeV,
               VulkanUtils::toVkAddressMode(addressMode))
          .set(&vk::SamplerCreateInfo::addressModeW,
               VulkanUtils::toVkAddressMode(addressMode))
          .set(&vk::SamplerCreateInfo::anisotropyEnable, (vk::Bool32)VK_TRUE)
          .set(&vk::SamplerCreateInfo::maxAnisotropy, 16.0F)
          .set(&vk::SamplerCreateInfo::unnormalizedCoordinates,
               (vk::Bool32)VK_FALSE)
          .set(&vk::SamplerCreateInfo::compareEnable,
               (vk::Bool32)(m_isShadowSampler ? VK_TRUE : VK_FALSE))
          .set(&vk::SamplerCreateInfo::compareOp,
               VulkanUtils::toVkCompareOp(compareOp))
          .set(&vk::SamplerCreateInfo::mipmapMode,
               minFilter == Filter::Linear ? vk::SamplerMipmapMode::eLinear
                                           : vk::SamplerMipmapMode::eNearest)
          .set(&vk::SamplerCreateInfo::mipLodBias, 0.0F)
          .set(&vk::SamplerCreateInfo::minLod, 0.0F)
          .set(&vk::SamplerCreateInfo::maxLod, (float)VK_LOD_CLAMP_NONE);

  if (addressMode == SamplerAddressMode::ClampToBorder &&
      compareOp != CompareOp::None) {
    samplerInfoBuilder.set(&vk::SamplerCreateInfo::borderColor,
                           vk::BorderColor::eFloatOpaqueWhite);
  } else {
    samplerInfoBuilder.set(&vk::SamplerCreateInfo::borderColor,
                           vk::BorderColor::eIntOpaqueBlack);
  }

  m_handle = m_device->device().createSampler(samplerInfoBuilder.build());
  m_device->trackObject(vk::ObjectType::eSampler,
                        pnkr::util::u64(static_cast<VkSampler>(m_handle)),
                        "Sampler");
}

    VulkanRHISampler::~VulkanRHISampler()
    {
        if (m_bindlessHandle.isValid()) {
            if (auto* bindless = m_device->getBindlessManager())
            {
                if (m_isShadowSampler) {
                    bindless->releaseShadowSampler(m_bindlessHandle);
                } else {
                    bindless->releaseSampler(m_bindlessHandle);
                }
            }
        }

        if (m_handle) {
            m_device->untrackObject(
                pnkr::util::u64(static_cast<VkSampler>(m_handle)));
            m_device->device().destroySampler(m_handle);
        }
    }

}

