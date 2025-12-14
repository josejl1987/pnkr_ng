//
// Created by Jose on 12/14/2025.
//

#ifndef PNKR_VULKANRENDERTARGET_H
#define PNKR_VULKANRENDERTARGET_H
#pragma once
#include <array>
#include "pnkr/renderer/vulkan/image/vulkan_image.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

    class VulkanRenderTarget {
    public:
        VulkanRenderTarget(VmaAllocator allocator,
                     vk::Device device,
                     uint32_t width,
                     uint32_t height,
                     vk::Format colorFormat,
                     vk::Format depthFormat);

        ~VulkanRenderTarget() = default;

        // Non-copyable, movable
        VulkanRenderTarget(const VulkanRenderTarget&) = delete;
        VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
        VulkanRenderTarget(VulkanRenderTarget&&) = default;
        VulkanRenderTarget& operator=(VulkanRenderTarget&&) = default;

        [[nodiscard]] const VulkanImage& colorImage() const { return m_colorImage; }
        [[nodiscard]] const VulkanImage& depthImage() const { return m_depthImage; }
        [[nodiscard]] vk::Extent2D extent() const { return {m_width, m_height}; }
        [[nodiscard]] vk::Viewport viewport() const;
        [[nodiscard]] vk::Rect2D scissor() const;

        // Helper to start rendering into this target
        void beginRendering(vk::CommandBuffer cmd,
                            vk::ClearValue colorClear = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}}},
                            vk::ClearValue depthClear = vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}});

        void endRendering(vk::CommandBuffer cmd);

        // Transition layouts for drawing
        void transitionToAttachment(vk::CommandBuffer cmd);

        // Transition color for reading (sampling/transfer)
        void transitionToRead(vk::CommandBuffer cmd);

    private:
        uint32_t m_width;
        uint32_t m_height;
        VulkanImage m_colorImage;
        VulkanImage m_depthImage;
    };

} // namespace pnkr::renderer

#endif //PNKR_VULKANRENDERTARGET_H
