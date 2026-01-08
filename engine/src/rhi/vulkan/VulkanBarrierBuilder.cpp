#include "rhi/vulkan/VulkanBarrierBuilder.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include "rhi/vulkan/vulkan_buffer.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "vulkan_cast.hpp"
#include "pnkr/core/common.hpp"

namespace pnkr::renderer::rhi::vulkan
{
    namespace
    {
        inline vk::Buffer unwrap(RHIBuffer* buf)
        {
            return rhi_cast<VulkanRHIBuffer>(buf)->buffer();
        }

        inline vk::Image unwrap(RHITexture* tex)
        {
            return {static_cast<VkImage>(tex->nativeHandle())};
        }

        uint32_t clampSubresourceCount(uint32_t base, uint32_t count, uint32_t total)
        {
            if (base >= total) {
                return 0U;
            }
            if (count == kInvalidBindlessIndex) {
                return total - base;
            }
            return std::min(count, total - base);
        }

        vk::AccessFlags2 accessForStageSrc(vk::PipelineStageFlags2 stage)
        {
            vk::AccessFlags2 access{};

            if (stage & vk::PipelineStageFlagBits2::eHost) {
                access |= vk::AccessFlagBits2::eHostWrite;
            }
            if (stage & vk::PipelineStageFlagBits2::eDrawIndirect) {
                access |= vk::AccessFlagBits2::eIndirectCommandRead;
            }

            if (stage & vk::PipelineStageFlagBits2::eTransfer) {
                access |= vk::AccessFlagBits2::eTransferWrite |
                          vk::AccessFlagBits2::eTransferRead;
            }
            if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput) {
                access |= vk::AccessFlagBits2::eColorAttachmentWrite;
            }
            if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests)) {
                access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            }

            const vk::PipelineStageFlags2 shaderStages =
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

            if (stage & shaderStages) {
                access |= vk::AccessFlagBits2::eShaderWrite;
            }

            if (access == vk::AccessFlags2{}) {
                access = vk::AccessFlagBits2::eMemoryWrite;
            }

            return access;
        }

        vk::AccessFlags2 accessForStageDst(vk::PipelineStageFlags2 stage)
        {
            vk::AccessFlags2 access{};

            if (stage & vk::PipelineStageFlagBits2::eHost) {
                access |= vk::AccessFlagBits2::eHostRead |
                          vk::AccessFlagBits2::eHostWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eTransfer) {
                access |= vk::AccessFlagBits2::eTransferRead |
                          vk::AccessFlagBits2::eTransferWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eDrawIndirect) {
                access |= vk::AccessFlagBits2::eIndirectCommandRead;
            }

            const vk::PipelineStageFlags2 shaderStages =
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader;

            if (stage & shaderStages) {
                access |= vk::AccessFlagBits2::eShaderRead |
                          vk::AccessFlagBits2::eUniformRead |
                          vk::AccessFlagBits2::eShaderWrite;
            }

            if (stage & vk::PipelineStageFlagBits2::eColorAttachmentOutput) {
                access |= vk::AccessFlagBits2::eColorAttachmentRead |
                          vk::AccessFlagBits2::eColorAttachmentWrite;
            }

            if (stage & (vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                         vk::PipelineStageFlagBits2::eLateFragmentTests)) {
                access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                          vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            }

            if (access == vk::AccessFlags2{}) {
                access = vk::AccessFlagBits2::eMemoryRead |
                         vk::AccessFlagBits2::eMemoryWrite;
            }

            return access;
        }

        vk::BufferMemoryBarrier2 createBufferBarrier(
            const RHIMemoryBarrier& barrier,
            vk::PipelineStageFlags2 globalSrcStage,
            vk::PipelineStageFlags2 globalDstStage,
            const auto& sanitizeStages,
            const auto& stripHostAccessIfNoHostStage)
        {
            vk::BufferMemoryBarrier2 vkBarrier{};

            vk::PipelineStageFlags2 explicitSrcStage = (barrier.srcAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.srcAccessStage))
                                                           : vk::PipelineStageFlags2{};
            vk::PipelineStageFlags2 explicitDstStage = (barrier.dstAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.dstAccessStage))
                                                           : vk::PipelineStageFlags2{};

            vkBarrier.srcStageMask = (explicitSrcStage != vk::PipelineStageFlags2{})
                                         ? explicitSrcStage
                                         : (globalSrcStage != vk::PipelineStageFlags2{}
                                                ? globalSrcStage
                                                : vk::PipelineStageFlagBits2::eAllCommands);
            vkBarrier.dstStageMask = (explicitDstStage != vk::PipelineStageFlags2{})
                                         ? explicitDstStage
                                         : (globalDstStage != vk::PipelineStageFlags2{}
                                                ? globalDstStage
                                                : vk::PipelineStageFlagBits2::eAllCommands);

            if (barrier.srcAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.srcStageMask |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.dstStageMask |= vk::PipelineStageFlagBits2::eHost;
            }

            vkBarrier.srcAccessMask = accessForStageSrc(vkBarrier.srcStageMask);
            vkBarrier.dstAccessMask = accessForStageDst(vkBarrier.dstStageMask);

            stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
            stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

            vkBarrier.buffer = unwrap(barrier.buffer);
            vkBarrier.size = VK_WHOLE_SIZE;
            vkBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamilyIndex;
            vkBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamilyIndex;

            return vkBarrier;
        }

        std::optional<vk::ImageMemoryBarrier2> createImageBarrier(
            const RHIMemoryBarrier& barrier,
            vk::PipelineStageFlags2 explicitSrcStage,
            vk::PipelineStageFlags2 explicitDstStage,
            const auto& sanitizeStages,
            const auto& stripHostAccessIfNoHostStage,
            const auto& sanitizeAccess)
        {
            if (barrier.texture == nullptr)
            {
                return std::nullopt;
            }

            vk::ImageMemoryBarrier2 vkBarrier{};
            vkBarrier.oldLayout = VulkanUtils::toVkImageLayout(barrier.oldLayout);
            vkBarrier.newLayout = VulkanUtils::toVkImageLayout(barrier.newLayout);

            auto [oldLayoutStage, oldLayoutAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.oldLayout);
            auto [newLayoutStage, newLayoutAccess] = VulkanUtils::getLayoutStageAccess(vkBarrier.newLayout);

            vkBarrier.srcStageMask = (explicitSrcStage != vk::PipelineStageFlags2{})
                                         ? sanitizeStages(explicitSrcStage | oldLayoutStage)
                                         : sanitizeStages(oldLayoutStage);
            vkBarrier.dstStageMask = (explicitDstStage != vk::PipelineStageFlags2{})
                                         ? sanitizeStages(explicitDstStage | newLayoutStage)
                                         : sanitizeStages(newLayoutStage);

            if (barrier.srcAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.srcStageMask |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host))
            {
                vkBarrier.dstStageMask |= vk::PipelineStageFlagBits2::eHost;
            }

            if (vkBarrier.oldLayout == vk::ImageLayout::eUndefined)
            {
                vkBarrier.srcAccessMask = {};
            }
            else
            {
                vkBarrier.srcAccessMask = oldLayoutAccess | accessForStageSrc(explicitSrcStage);
            }
            vkBarrier.dstAccessMask = newLayoutAccess | accessForStageDst(explicitDstStage);

            stripHostAccessIfNoHostStage(vkBarrier.srcStageMask, vkBarrier.srcAccessMask);
            stripHostAccessIfNoHostStage(vkBarrier.dstStageMask, vkBarrier.dstAccessMask);

            vkBarrier.srcAccessMask = sanitizeAccess(vkBarrier.srcAccessMask);
            vkBarrier.dstAccessMask = sanitizeAccess(vkBarrier.dstAccessMask);

            vkBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.srcQueueFamilyIndex;
            vkBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex == kQueueFamilyIgnored ? VK_QUEUE_FAMILY_IGNORED : barrier.dstQueueFamilyIndex;
            vkBarrier.image = unwrap(barrier.texture);

            const vk::Format fmt = VulkanUtils::toVkFormat(barrier.texture->format());
            vkBarrier.subresourceRange.aspectMask = VulkanUtils::getImageAspectMask(fmt);

            const uint32_t texMipLevels =
                std::max(1U, barrier.texture->mipLevels());
            const uint32_t texArrayLayers =
                std::max(1U, barrier.texture->arrayLayers());

            vkBarrier.subresourceRange.baseMipLevel = barrier.baseMipLevel;
            vkBarrier.subresourceRange.levelCount = clampSubresourceCount(
                barrier.baseMipLevel, barrier.levelCount, texMipLevels);
            vkBarrier.subresourceRange.baseArrayLayer = barrier.baseArrayLayer;
            vkBarrier.subresourceRange.layerCount = clampSubresourceCount(
                barrier.baseArrayLayer, barrier.layerCount, texArrayLayers);

            if (vkBarrier.subresourceRange.levelCount == 0 || vkBarrier.subresourceRange.layerCount == 0)
            {
                return std::nullopt;
            }

            return vkBarrier;
        }
    }

    void VulkanBarrierBuilder::buildBarriers(
        const VulkanRHIDevice& device,
        uint32_t queueFamilyIndex,
        ShaderStageFlags srcStage,
        ShaderStageFlags dstStage,
        std::span<const RHIMemoryBarrier> barriers,
        std::vector<vk::BufferMemoryBarrier2>& outBufferBarriers,
        std::vector<vk::ImageMemoryBarrier2>& outImageBarriers)
    {
        const auto& caps = device.physicalDevice().capabilities();
        auto sanitizeStages = [&](vk::PipelineStageFlags2 stages)
        {
            if (!caps.tessellationShader)
            {
                stages &= ~vk::PipelineStageFlagBits2::eTessellationControlShader;
                stages &= ~vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
            }
            if (!caps.geometryShader)
            {
                stages &= ~vk::PipelineStageFlagBits2::eGeometryShader;
            }

            if (queueFamilyIndex != device.graphicsQueueFamily() &&
                queueFamilyIndex != device.computeQueueFamily())
            {
                vk::PipelineStageFlags2 validTransferStages =
                    vk::PipelineStageFlagBits2::eTransfer |
                    vk::PipelineStageFlagBits2::eTopOfPipe |
                    vk::PipelineStageFlagBits2::eBottomOfPipe |
                    vk::PipelineStageFlagBits2::eHost |
                    vk::PipelineStageFlagBits2::eAllCommands;

                stages &= validTransferStages;
            }

            return stages;
        };

        auto stripHostAccessIfNoHostStage =
            [](vk::PipelineStageFlags2 stages, vk::AccessFlags2& access)
        {
            const vk::AccessFlags2 hostBits =
                vk::AccessFlagBits2::eHostRead | vk::AccessFlagBits2::eHostWrite;
            if (!(stages & vk::PipelineStageFlagBits2::eHost))
            {
                access &= ~hostBits;
            }
        };

        const bool isTransferOnlyQueue = (queueFamilyIndex != device.graphicsQueueFamily() &&
                                          queueFamilyIndex != device.computeQueueFamily());
        auto sanitizeAccess = [isTransferOnlyQueue](vk::AccessFlags2 access)
        {
            if (isTransferOnlyQueue)
            {
                vk::AccessFlags2 validTransferAccess =
                    vk::AccessFlagBits2::eTransferRead |
                    vk::AccessFlagBits2::eTransferWrite |
                    vk::AccessFlagBits2::eHostRead |
                    vk::AccessFlagBits2::eHostWrite |
                    vk::AccessFlagBits2::eMemoryRead |
                    vk::AccessFlagBits2::eMemoryWrite;
                access &= validTransferAccess;
            }
            return access;
        };

        const vk::PipelineStageFlags2 globalSrcStageMask = sanitizeStages(VulkanUtils::toVkPipelineStage(srcStage));
        const vk::PipelineStageFlags2 globalDstStageMask = sanitizeStages(VulkanUtils::toVkPipelineStage(dstStage));

        outBufferBarriers.reserve(outBufferBarriers.size() + barriers.size());
        outImageBarriers.reserve(outImageBarriers.size() + barriers.size());

        for (const auto& barrier : barriers)
        {
            vk::PipelineStageFlags2 explicitSrcStage = (barrier.srcAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.srcAccessStage))
                                                           : vk::PipelineStageFlags2{};
            vk::PipelineStageFlags2 explicitDstStage = (barrier.dstAccessStage != ShaderStage::None)
                                                           ? sanitizeStages(
                                                               VulkanUtils::toVkPipelineStage(barrier.dstAccessStage))
                                                           : vk::PipelineStageFlags2{};

            if (barrier.srcAccessStage.has(ShaderStage::Host)) {
              explicitSrcStage |= vk::PipelineStageFlagBits2::eHost;
            }
            if (barrier.dstAccessStage.has(ShaderStage::Host)) {
              explicitDstStage |= vk::PipelineStageFlagBits2::eHost;
            }

            if (barrier.buffer != nullptr)
            {
                outBufferBarriers.push_back(createBufferBarrier(
                    barrier, globalSrcStageMask, globalDstStageMask, sanitizeStages, stripHostAccessIfNoHostStage));
            }
            else if (barrier.texture != nullptr)
            {
                auto vkBarrier = createImageBarrier(
                    barrier, explicitSrcStage, explicitDstStage, sanitizeStages, stripHostAccessIfNoHostStage, sanitizeAccess);
                if (vkBarrier.has_value())
                {
                    outImageBarriers.push_back(*vkBarrier);
                }
            }
        }
    }
}
