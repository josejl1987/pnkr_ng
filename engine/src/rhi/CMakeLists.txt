# ============================================================================
# RHI (Render Hardware Interface) Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    rhi_pipeline_builder.cpp
    rhi_shader.cpp

  PUBLIC FILE_SET headers BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include" FILES
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/BindlessManager.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_buffer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_command_buffer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_descriptor.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_device.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_factory.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_imgui.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_pipeline.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_pipeline_builder.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_sampler.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_shader.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_swapchain.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_sync.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_texture.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/rhi/rhi_types.hpp"
)

# Add Vulkan backend subdirectory
add_subdirectory(vulkan)
