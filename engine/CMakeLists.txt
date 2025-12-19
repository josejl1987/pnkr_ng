# ============================================================================
# PNKR Engine Static Library
# ============================================================================
add_library(pnkr_engine STATIC
        src/core/implementations.cpp
        src/renderer/vulkan/vulkan_dispatch.cpp
        src/renderer/vulkan/vulkan_context.cpp
        include/pnkr/renderer/vulkan/vulkan_sync_manager.h
        src/renderer/vulkan/vulkan_sync_manager.cpp
        include/pnkr/renderer/vulkan/vma.cpp
        include/pnkr/renderer/geometry/Vertex.h
        include/pnkr/renderer/geometry/VertexInputDescription.h
        include/pnkr/renderer/vulkan/pipeline/PipelineConfig.h
        include/pnkr/renderer/pipeline/Pipeline.h
        include/pnkr/renderer/geometry/mesh.h
        src/renderer/geometry/mesh.cpp
        include/pnkr/core/Handle.h
        include/pnkr/renderer/vulkan/PushConstants.h
        include/pnkr/renderer/scene/camera.hpp
        src/renderer/scene.cpp
        src/renderer/scene/Model.cpp
        include/pnkr/core/Timer.h
        include/pnkr/renderer/vulkan/image/vulkan_image.hpp
        src/renderer/vulkan/image/vulkan_image.cpp
        include/pnkr/renderer/vulkan/image/vulkan_sampler.hpp
        src/renderer/vulkan/image/vulkan_sampler.cpp
        src/renderer/vulkan/vulkan_descriptor.cpp
        include/pnkr/renderer/scene/Model.hpp
        include/pnkr/renderer/scene/Node.hpp
        include/pnkr/renderer/scene/RHIScene.hpp
        include/pnkr/renderer/scene/Skybox.hpp
        include/pnkr/renderer/material/Material.hpp
        include/pnkr/renderer/vulkan/vulkan_render_target.h
        src/renderer/vulkan/vulkan_render_target.cpp
        include/pnkr/renderer/vulkan/pipeline/PipelineBuilder.h
        src/renderer/vulkan/pipeline/pipeline_builder.cpp
        include/pnkr/ui/imgui_layer.hpp
        src/renderer/ui/imgui_layer.cpp
        include/pnkr/renderer/vulkan/pipeline/compute_pipeline.hpp
        src/renderer/vulkan/pipeline/compute_pipeline.cpp
        include/pnkr/renderer/vulkan/bindless/bindless_manager.hpp
        src/renderer/vulkan/bindless/bindless_manager.cpp
        src/rhi/vulkan/vulkan_buffer.cpp
        src/rhi/vulkan/vulkan_command_buffer.cpp
        src/rhi/vulkan/vulkan_texture.cpp
        src/rhi/vulkan/vulkan_device.cpp
        src/rhi/vulkan/vulkan_descriptor.cpp
        src/rhi/vulkan/vulkan_swapchain.cpp
        src/rhi/vulkan/vulkan_utils.cpp
        src/rhi/vulkan/vulkan_buffer.cpp

        src/rhi/vulkan/vulkan_texture.cpp
        src/rhi/vulkan/vulkan_pipeline.cpp
        src/rhi/vulkan/vulkan_sampler.cpp
        src/rhi/vulkan/rhi_factory.cpp
        include/pnkr/renderer/rhi_renderer.hpp
        src/renderer/rhi_renderer.cpp
        include/pnkr/rhi/rhi_pipeline_builder.hpp
        src/rhi/rhi_pipeline_builder.cpp
        include/pnkr/rhi/rhi_shader.hpp
        src/rhi/rhi_shader.cpp
        include/pnkr/renderer/scene/RHIScene.hpp
        src/renderer/scene/RHIScene.cpp
        include/pnkr/renderer/scene/Skybox.hpp
        src/renderer/scene/Skybox.cpp
        include/pnkr/renderer/debug/LineCanvas3D.hpp
        src/renderer/debug/LineCanvas3D.cpp
        include/pnkr/renderer/debug/DebugLayer.hpp
        src/renderer/debug/DebugLayer.cpp
        src/renderer/debug/shaders/line_canvas.vert
        src/renderer/debug/shaders/line_canvas.frag)

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
      include/pnkr/core/profiler.hpp
      include/pnkr/core/common.hpp
      include/pnkr/platform/window.hpp
      include/pnkr/renderer/renderer_config.hpp
      include/pnkr/renderer/renderer.hpp
      include/pnkr/rhi/rhi_descriptor.hpp
      include/pnkr/rhi/rhi_swapchain.hpp
      include/pnkr/rhi/vulkan/vulkan_swapchain.hpp
      include/pnkr/rhi/vulkan/vulkan_descriptor.hpp
        include/pnkr/renderer/vulkan/vulkan_buffer.hpp
        include/pnkr/renderer/vulkan/vulkan_command_buffer.hpp
        include/pnkr/renderer/vulkan/vulkan_context.hpp
        include/pnkr/renderer/vulkan/vulkan_device.hpp
        include/pnkr/renderer/vulkan/vulkan_pipeline.hpp
        include/pnkr/renderer/vulkan/vulkan_swapchain.hpp
        include/pnkr/renderer/scene/Model.hpp
        include/pnkr/renderer/scene/Node.hpp
        include/pnkr/renderer/scene/RHIScene.hpp
        include/pnkr/renderer/scene/Skybox.hpp
        include/pnkr/renderer/material/Material.hpp
)

# ============================================================================
# Debug Layer Shader Compilation
# ============================================================================
find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/bin32)
if(NOT GLSLC_EXECUTABLE)
  message(STATUS "glslc not found - DebugLayer shaders will not be compiled")
else()
  # Set output directory for compiled shaders
  set(DEBUG_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

  # Ensure output directory exists
  file(MAKE_DIRECTORY ${DEBUG_SHADER_OUTPUT_DIR})

  # Compile debug layer shaders
  add_custom_command(
    OUTPUT ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.vert.spv ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.frag.spv
    COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/debug/shaders/line_canvas.vert -o ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.vert.spv
    COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/debug/shaders/line_canvas.frag -o ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/debug/shaders/line_canvas.vert ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/debug/shaders/line_canvas.frag
    COMMENT "Compiling DebugLayer shaders to SPIR-V"
  )

  # Create a custom target for shader compilation
  add_custom_target(debug_shaders ALL DEPENDS ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.vert.spv ${DEBUG_SHADER_OUTPUT_DIR}/line_canvas.frag.spv)

  # Make the engine depend on shader compilation
  add_dependencies(pnkr_engine debug_shaders)
endif()

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
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    TRACY_ENABLE=1
    PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1

)

# Unity build to reduce per-TU header parsing overhead
set_target_properties(pnkr_engine PROPERTIES
  UNITY_BUILD OFF
  UNITY_BUILD_BATCH_SIZE 8
)
set_source_files_properties(src/core/implementations.cpp PROPERTIES UNITY_BUILD OFF)

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
find_package(imgui CONFIG REQUIRED)

target_link_libraries(pnkr_engine PRIVATE imgui::imgui)
target_include_directories(pnkr_engine PUBLIC "${VMA_INCLUDE_DIR}")
target_compile_definitions(pnkr_engine PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy Profiler")

if(TRACY_ENABLE)
  CPMAddPackage(
          NAME tracy
          GITHUB_REPOSITORY wolfpld/tracy
          GIT_TAG v0.13.1
          OPTIONS
          "TRACY_ENABLE ON"
          "TRACY_ON_DEMAND ON"      # Starts profiling only when server connects
          "TRACY_NO_BROADCAST OFF"
          "TRACY_NO_SYSTEM_VULKAN OFF" # Rely on the Vulkan SDK found by CMake
  )
endif()

find_package(tracy CONFIG REQUIRED)
target_link_libraries(pnkr_engine
  PUBLIC
        Tracy::TracyClient
)

find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)
find_package(spirv_cross_hlsl CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

target_link_libraries(pnkr_engine
        PUBLIC
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-cpp
        spirv-cross-hlsl
        fastgltf::fastgltf
)

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


# --- Shaders ---
# Uses ShaderTools.cmake to compile and generate C++ structs
add_shader_target("shaders/skybox.vert")
add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

add_shader_target("shaders/skybox.frag")
add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

# Include generated headers
target_include_directories(pnkr_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Rebuild if shader headers change
set_source_files_properties( src/renderer/scene/Skybox.cpp PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/skybox.vert.h"
)