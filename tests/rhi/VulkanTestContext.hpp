#pragma once

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/rhi_device.hpp"
#include "pnkr/rhi/rhi_factory.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include <memory>
#include <vulkan/vulkan.hpp>

namespace pnkr::tests {

class VulkanTestContext {
public:
  VulkanTestContext();
  ~VulkanTestContext();

  void setup();
  void teardown();

  pnkr::renderer::rhi::RHIDevice *device() const;
  vk::Instance vulkanInstance() const;
  vk::PhysicalDevice physicalDevice() const;

private:
  bool createInstance();
  bool createHeadlessSurface();
  bool isHeadlessRequired() const;
  void validateLavapipeAvailable();
  bool selectLavapipeDevice();

  std::unique_ptr<pnkr::renderer::rhi::RHIDevice> m_device;
  std::shared_ptr<pnkr::renderer::rhi::vulkan::VulkanInstanceContext>
      m_instanceContext;
  vk::PhysicalDevice m_physicalDevice{};
  vk::SurfaceKHR m_headlessSurface{};
  PFN_vkCreateHeadlessSurfaceEXT m_vkCreateHeadlessSurfaceEXT = nullptr;
  bool m_headlessSupported = false;
  bool m_isSetup = false;
};

} // namespace pnkr::tests
