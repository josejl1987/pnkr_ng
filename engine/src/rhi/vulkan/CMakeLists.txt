# ============================================================================
# Vulkan RHI Backend Module
# ============================================================================

# Local defines for Vulkan implementation
target_compile_definitions(pnkr_engine PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

target_sources(pnkr_engine
  PRIVATE
    BDARegistry.cpp
    BindlessDescriptorManager.cpp
    DescriptorUpdater.cpp
    rhi_factory.cpp
    vulkan_buffer.cpp
    vulkan_command_buffer.cpp
    vulkan_descriptor.cpp
    vulkan_device.cpp
    vulkan_gpu_profiler.cpp
    vulkan_imgui.cpp
    vulkan_pipeline.cpp
    vulkan_sampler.cpp
    vulkan_swapchain.cpp
    vulkan_sync.cpp
    vulkan_texture.cpp
    vulkan_utils.cpp
    VulkanResourceFactory.cpp
    VulkanDeletionQueue.cpp
    VulkanSyncManager.cpp
    VulkanBarrierBuilder.cpp
    AftermathIntegration.cpp
)

if(PNKR_ENABLE_AFTERMATH)
  target_link_libraries(pnkr_engine PRIVATE nvidia_aftermath)
endif()
