

#ifndef PNKR_VULKANIMAGE_H
#define PNKR_VULKANIMAGE_H

#include <filesystem>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

    class VulkanDevice;

    class VulkanImage {
    public:
        VulkanImage(VmaAllocator allocator,
                    uint32_t width,
                    uint32_t height,
                    vk::Format format,
                    vk::ImageTiling tiling,
                    vk::ImageUsageFlags usage,
                    VmaMemoryUsage memoryUsage,
                    vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor);

        ~VulkanImage();

        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;

        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;
        static VulkanImage createFromFile(const VulkanDevice& device,
                                          const std::filesystem::path& filepath,
                                          bool srgb = true);

        static VulkanImage createFromMemory(const VulkanDevice& vulkan_device,
                                            const unsigned char* src_data,
                                            int width,
                                            int height,
                                            bool srgb);

        void transitionLayout(vk::CommandBuffer cmd,
                              vk::ImageLayout oldLayout,
                              vk::ImageLayout newLayout,
                              vk::PipelineStageFlags srcStage,
                              vk::PipelineStageFlags dstStage);

        [[nodiscard]] vk::Image image() const noexcept { return m_image; }
        [[nodiscard]] vk::ImageView view() const noexcept { return m_view; }
        [[nodiscard]] vk::Format format() const noexcept { return m_format; }
        [[nodiscard]] uint32_t width() const noexcept { return m_width; }
        [[nodiscard]] uint32_t height() const noexcept { return m_height; }

    private:
        void createImageView(vk::Device device, vk::ImageAspectFlags aspectFlags);
        void destroy() noexcept;

        VmaAllocator m_allocator{nullptr};
        vk::Image m_image{};
        vk::ImageView m_view{};
        VmaAllocation m_allocation{nullptr};

        vk::Format m_format{vk::Format::eUndefined};
        uint32_t m_width{0};
        uint32_t m_height{0};
        vk::Device m_device{};
    };

}

#endif
