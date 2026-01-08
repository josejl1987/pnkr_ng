#pragma once

#include "pnkr/rhi/rhi_swapchain.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include "pnkr/core/profiler.hpp"
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class VulkanRHISwapchainImage final : public RHITexture
    {
    public:
        VulkanRHISwapchainImage(vk::Image image, vk::ImageView view, const Extent3D& extent, Format fmt)
            : m_image(image), m_view(view), m_extent(extent), m_format(fmt) {}

        void uploadData(std::span<const std::byte>, const TextureSubresource& = {}) override {}
        void generateMipmaps() override {}

        const Extent3D& extent() const override { return m_extent; }
        Format format() const override { return m_format; }
        uint32_t mipLevels() const override { return 1; }
        uint32_t arrayLayers() const override { return 1; }
        uint32_t sampleCount() const override { return 1; }
        TextureUsageFlags usage() const override { return TextureUsage::ColorAttachment | TextureUsage::TransferDst | TextureUsage::TransferSrc; }

        void* nativeHandle() const override { return static_cast<VkImage>(m_image); }
        void* nativeView() const override { return static_cast<VkImageView>(m_view); }
        void* nativeView(uint32_t mipLevel, uint32_t arrayLayer) const override {

            if (mipLevel == 0 && arrayLayer == 0) {
                return static_cast<VkImageView>(m_view);
            }
            return nullptr;
        }

        vk::Image image() const { return m_image; }
        vk::ImageView imageView() const { return m_view; }
        void generateMipmaps(RHICommandList* cmd) override;

    private:
        vk::Image m_image{};
        vk::ImageView m_view{};
        Extent3D m_extent{};
        Format m_format{Format::Undefined};
    };

    class VulkanRHISwapchain final : public RHISwapchain
    {
    public:
        VulkanRHISwapchain(VulkanRHIDevice* device, platform::Window& window, Format preferredFormat);
        ~VulkanRHISwapchain() override;

        Format colorFormat() const override { return m_rhiFormat; }
        Extent2D extent() const override { return { m_extent.width, m_extent.height }; }
        uint32_t imageCount() const override { return static_cast<uint32_t>(m_images.size()); }
        uint32_t framesInFlight() const override { return m_framesInFlight; }

        bool beginFrame(uint32_t frameIndex, RHICommandList* cmd, SwapchainFrame& out) override;
        bool endFrame(uint32_t frameIndex, RHICommandList* cmd) override;
        bool present(uint32_t frameIndex) override;

        void recreate(uint32_t width, uint32_t height) override;
        void setVsync(bool enabled) override { m_vsync = enabled; }
        ResourceLayout currentLayout() const override { return m_layouts[m_currentImage]; }
        void* getProfilingContext() const override { return m_tracyContext; }

    private:
        bool m_vsync = true;
        VulkanRHIDevice* m_device{};
        platform::Window* m_window{};
        TracyContext m_tracyContext{};

        vk::SurfaceKHR m_surface{};
        vk::SwapchainKHR m_swapchain{};

        vk::Format m_vkFormat{vk::Format::eUndefined};
        Format m_rhiFormat{Format::Undefined};
        vk::Extent2D m_extent{};

        std::vector<vk::Image> m_images;
        std::vector<vk::ImageView> m_views;
        std::vector<std::unique_ptr<VulkanRHISwapchainImage>> m_wrapped;
        std::vector<ResourceLayout> m_layouts;

        uint32_t m_currentImage = 0;
        uint32_t m_currentFrameIndex = 0;

        uint32_t m_framesInFlight = 3;
        std::vector<vk::Semaphore> m_imageAvailable;

        std::vector<vk::Semaphore> m_renderFinished;

    public:
        vk::Semaphore getCurrentAcquireSemaphore() const {
            return m_imageAvailable[m_currentFrameIndex % m_framesInFlight];
        }
        vk::Semaphore getCurrentRenderFinishedSemaphore() const {
            return m_renderFinished[m_currentImage];
        }

        TracyContext getTracyContext() const { return m_tracyContext; }

        void createSurface();
        void createSwapchain(Format preferredFormat, uint32_t width, uint32_t height);
        void destroySwapchain();

        void createSyncObjects();
        void destroySyncObjects();

        static vk::SurfaceFormatKHR
        chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &formats,
                            Format preferred);
        vk::PresentModeKHR
        choosePresentMode(const std::vector<vk::PresentModeKHR> &modes) const;
        static vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR &caps,
                                         uint32_t width, uint32_t height);
    };

}
