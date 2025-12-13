# ============================================================================
# PNKR Engine Static Library
# ============================================================================
add_library(pnkr_engine STATIC
        src/renderer/vulkan/vulkan_dispatch.cpp
        src/renderer/vulkan/vulkan_context.cpp
        include/pnkr/renderer/vulkan/vulkan_sync_manager.h
        src/renderer/vulkan/vulkan_sync_manager.cpp
        include/pnkr/renderer/vulkan/vma.cpp)

target_sources(pnkr_engine
  PRIVATE
    src/core/logger.cpp
    src/platform/window.cpp
    src/renderer/renderer.cpp
        src/renderer/vulkan/vulkan_buffer.cpp
        src/renderer/vulkan/vulkan_command_buffer.cpp
        src/renderer/vulkan/vulkan_device.cpp
        src/renderer/vulkan/vulkan_pipeline.cpp
        src/renderer/vulkan/vulkan_swapchain.cpp
        src/renderer/vulkan/vulkan_context.cpp

        PUBLIC
    FILE_SET headers
    TYPE HEADERS
    FILES
      include/pnkr/engine.hpp
      include/pnkr/core/logger.hpp
      include/pnkr/core/common.hpp
      include/pnkr/platform/window.hpp
      include/pnkr/renderer/renderer.hpp
        include/pnkr/renderer/vulkan/vulkan_buffer.hpp
        include/pnkr/renderer/vulkan/vulkan_command_buffer.hpp
        include/pnkr/renderer/vulkan/vulkan_context.hpp
        include/pnkr/renderer/vulkan/vulkan_device.hpp
        include/pnkr/renderer/vulkan/vulkan_pipeline.hpp
        include/pnkr/renderer/vulkan/vulkan_swapchain.hpp
)

# ============================================================================
# Compilation Settings
# ============================================================================
target_compile_features(pnkr_engine PUBLIC cxx_std_20)

target_include_directories(pnkr_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(pnkr_engine
  PUBLIC
    SPDLOG_USE_STD_FORMAT=1
  PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

# Unity build to reduce per-TU header parsing overhead
set_target_properties(pnkr_engine PROPERTIES
  UNITY_BUILD ON
  UNITY_BUILD_BATCH_SIZE 8
)

# Precompiled headers to avoid repeatedly parsing heavy includes like vulkan.hpp
target_precompile_headers(pnkr_engine PRIVATE
  <vector>
  <memory>
  <string>
  <stdexcept>
  <vulkan/vulkan.hpp>
  <spdlog/spdlog.h>
  <SDL3/SDL.h>
)

# ============================================================================
# Dependencies
# ============================================================================
# Prefer header-only spdlog to avoid DLL/import-lib mismatches across toolchains
if(TARGET spdlog::spdlog_header_only)
  set(PNKR_SPDLOG_TARGET spdlog::spdlog_header_only)
  target_compile_definitions(pnkr_engine PUBLIC SPDLOG_HEADER_ONLY=1)
else()
  set(PNKR_SPDLOG_TARGET spdlog::spdlog)
endif()

target_link_libraries(pnkr_engine
  PUBLIC
    ${PNKR_SPDLOG_TARGET}
    SDL3::SDL3
    Vulkan::Vulkan
)

# GLM (this one is usually a proper CMake package)
find_package(glm CONFIG REQUIRED)
target_link_libraries(pnkr_engine PUBLIC glm::glm)

# VMA (vcpkg port may not export an imported target; treat as header-only unless a target exists)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)   # provides GPUOpen::VulkanMemoryAllocator

target_link_libraries(pnkr_engine
        PUBLIC
        GPUOpen::VulkanMemoryAllocator
)
target_include_directories(pnkr_engine PUBLIC "${VMA_INCLUDE_DIR}")
target_compile_definitions(pnkr_engine PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)




# ============================================================================
# Compiler Warnings (Per-target)
# ============================================================================
if(MSVC)
  target_compile_options(pnkr_engine PRIVATE /W4 /WX)
else()
  target_compile_options(pnkr_engine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter  # SDL3 headers trigger this
  )
endif()

# ============================================================================
# Optimization
# ============================================================================
if(MSVC)
  target_compile_options(pnkr_engine PRIVATE
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Debug>:/Od>
  )
else()
  target_compile_options(pnkr_engine PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-g -O0>
  )
endif()

# ============================================================================
# Install Configuration (for Stage 1+)
# ============================================================================
install(TARGETS pnkr_engine
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
