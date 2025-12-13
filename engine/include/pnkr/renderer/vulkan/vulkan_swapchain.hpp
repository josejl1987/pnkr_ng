#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

namespace pnkr::platform
{
    class Window;
}

namespace pnkr::renderer
{
    class VulkanSwapchain
    {
    public:
        VulkanSwapchain(vk::PhysicalDevice physicalDevice,
                        vk::Device device,
                        vk::SurfaceKHR surface,
                        uint32_t graphicsQueueFamily,
                        uint32_t presentQueueFamily,
                        pnkr::platform::Window& window);

        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        void recreate(vk::PhysicalDevice physicalDevice,
                      vk::Device device,
                      vk::SurfaceKHR surface,
                      uint32_t graphicsQueueFamily,
                      uint32_t presentQueueFamily,
                      pnkr::platform::Window& window);

        [[nodiscard]] vk::SwapchainKHR swapchain() const noexcept { return m_swapchain; }
        [[nodiscard]] vk::Format imageFormat() const noexcept { return m_format; }
        [[nodiscard]] vk::Extent2D extent() const noexcept { return m_extent; }

        [[nodiscard]] const std::vector<vk::Image>& images() const noexcept { return m_images; }
        [[nodiscard]] const std::vector<vk::ImageView>& imageViews() const noexcept { return m_imageViews; }

        vk::Result acquireNextImage(uint64_t timeoutNs,
                                    vk::Semaphore imageAvailable,
                                    vk::Fence fence,
                                    uint32_t& outImageIndex);

        vk::Result present(vk::Queue presentQueue,
                           uint32_t imageIndex,
                           vk::Semaphore renderFinished);
        vk::ImageLayout& imageLayout(uint32_t index) { return m_imageLayouts[index]; }
        vk::ImageLayout  imageLayout(uint32_t index) const { return m_imageLayouts[index]; }
    private:
        void destroy(vk::Device device);

        void createSwapchain(vk::PhysicalDevice physicalDevice,
                             vk::Device device,
                             vk::SurfaceKHR surface,
                             uint32_t graphicsQueueFamily,
                             uint32_t presentQueueFamily,
                             pnkr::platform::Window& window);

        void createImageViews(vk::Device device);

        static vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
        static vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& modes);
        static vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& caps,
                                         pnkr::platform::Window& window);

        vk::SwapchainKHR m_swapchain{nullptr};
        vk::Format m_format{};
        vk::Extent2D m_extent{};
        vk::Device m_device{nullptr};
        std::vector<vk::Image> m_images;
        std::vector<vk::ImageView> m_imageViews;
        std::vector<vk::ImageLayout> m_imageLayouts;

    };
} // namespace pnkr::renderer
