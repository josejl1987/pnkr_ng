#pragma once

#include <vulkan/vulkan.hpp>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer {

  class VulkanContext {
  public:
    explicit VulkanContext(pnkr::platform::Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]] vk::Instance     instance() const noexcept { return m_instance; }
    [[nodiscard]] vk::SurfaceKHR   surface()  const noexcept { return m_surface;  }

  private:
    void initDispatcher();
    void createInstance(pnkr::platform::Window& window);
    void setupDebugMessenger();
    void createSurface(pnkr::platform::Window& window);

    vk::detail::DynamicLoader m_dynamicLoader{};
    vk::Instance      m_instance{};
    vk::SurfaceKHR    m_surface{};

#ifndef NDEBUG
    vk::DebugUtilsMessengerEXT m_debugMessenger{};
#endif
  };

} // namespace pnkr::renderer
