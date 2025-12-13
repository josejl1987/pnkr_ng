#pragma once

#include <vulkan/vulkan.hpp>

namespace pnkr::platform {
class Window;
}

namespace pnkr::renderer {

class VulkanContext {
public:
  explicit VulkanContext(const pnkr::platform::Window &window);
  ~VulkanContext();

  VulkanContext(const VulkanContext &) = delete;
  VulkanContext &operator=(const VulkanContext &) = delete;

  VulkanContext(VulkanContext &&) = delete;
  VulkanContext &operator=(VulkanContext &&) = delete;

  [[nodiscard]] vk::Instance instance() const noexcept { return m_instance; }
  [[nodiscard]] vk::SurfaceKHR surface() const noexcept { return m_surface; }
  [[nodiscard]] const vk::detail::DispatchLoaderDynamic &
  dispatcher() const noexcept {
    return m_dld;
  }
  void initDispatcherPostDevice(vk::Device device);

private:
  void createInstance(const pnkr::platform::Window &window);
  void setupDebugMessenger();
  void createSurface(const pnkr::platform::Window &window);
  void initDispatcherPreInstance();
  void initDispatcherPostInstance();

  vk::detail::DynamicLoader m_dynamicLoader;
  vk::Instance m_instance{};
  vk::SurfaceKHR m_surface{};
  vk::detail::DispatchLoaderDynamic m_dld = {};

#ifndef NDEBUG
  vk::DebugUtilsMessengerEXT m_debugMessenger{};
  void (*(*m_vkGetInstanceProcAddr)(VkInstance_T *, const char *))();
#endif
};

} // namespace pnkr::renderer
