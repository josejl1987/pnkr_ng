# ============================================================================
# PNKR Engine Static Library
# ============================================================================

# PNKR Engine Static Library
# ============================================================================

# Create the library target
set(CMP_CORE_SOURCES
    src/assets/compressonator/source/cmp_core.cpp
    src/assets/compressonator/source/core_simd_sse.cpp
    src/assets/compressonator/source/core_simd_avx.cpp
    src/assets/compressonator/shaders/bc7_encode_kernel.cpp
    src/assets/compressonator/cmp_math/cpu_extensions.cpp
    src/assets/compressonator/cmp_math/cmp_math_common.cpp
)

add_library(pnkr_engine STATIC ""
         src/renderer/passes/WBOITPass.cpp
         src/renderer/passes/OITPass.cpp
         src/assets/BC7Encoder.cpp
         ${CMP_CORE_SOURCES}
         )

target_compile_definitions(pnkr_engine PRIVATE ENABLE_MAKE_COMPATIBLE_API)

if(MSVC)
    set_source_files_properties(${CMP_CORE_SOURCES} PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    # Disable PCH for compressonator files to avoid CPU mismatch errors
    set_source_files_properties(${CMP_CORE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif()


# ============================================================================
# Include Directories
# ============================================================================
target_include_directories(pnkr_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/compressonator/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/compressonator/shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/compressonator/cmp_math
)

# ============================================================================
# Compile Definitions
# ============================================================================
target_compile_definitions(pnkr_engine
  PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    TRACY_ENABLE=1
    $<$<CONFIG:Debug>:PNKR_DEBUG=1>
  PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)


# ============================================================================
# Compile Features and Options
# ============================================================================
target_compile_features(pnkr_engine PUBLIC cxx_std_23)

if(MSVC)
  target_compile_options(pnkr_engine PRIVATE /W4)
  
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl specific optimizations
    target_compile_options(pnkr_engine PRIVATE
      # We use /fp:fast but re-enable infinities since the engine relies on them (e.g. GLTF volume attenuation)
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2 /Ob3 /arch:AVX2 /fp:fast /clang:-fno-finite-math-only /clang:-Wno-nan-infinity-disabled -flto>
      $<$<CONFIG:Debug>:/Od /Z7>
      /Zi
    )
    target_link_options(pnkr_engine PUBLIC
      /DEBUG
      $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto>
    )
  else()
    # MSVC cl.exe specific optimizations
    target_compile_options(pnkr_engine PRIVATE
      # Use /fp:precise for cl.exe to ensure infinity/NaN checks work correctly
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2 /Ob3 /GL /arch:AVX2 /fp:precise>
      $<$<CONFIG:Debug>:/Od>
      /Zi
    )
    target_link_options(pnkr_engine PUBLIC
      /DEBUG /OPT:REF /OPT:ICF
      $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/LTCG>
    )
  endif()
else()
  target_compile_options(pnkr_engine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=x86-64-v3 -mtune=native -ffast-math -flto=auto -fno-semantic-interposition>
    $<$<CONFIG:Debug>:-g -O0>
  )
  target_link_options(pnkr_engine PUBLIC
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto=auto>
  )
endif()

# ============================================================================
# Unity Build Settings
# ============================================================================
set_target_properties(pnkr_engine PROPERTIES
  UNITY_BUILD OFF
  UNITY_BUILD_BATCH_SIZE 15
)
set_source_files_properties(src/core/implementations.cpp PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(src/assets/AssetImporter.cpp PROPERTIES UNITY_BUILD OFF)

# ============================================================================
# Precompiled Headers
# ============================================================================
if(NOT PNKR_ENABLE_COVERAGE)
  target_precompile_headers(pnkr_engine PRIVATE
    <vector>
    <memory>
    <string>
    <stdexcept>
    <vulkan/vulkan.hpp>
    <vk_mem_alloc.h>
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <fastgltf/core.hpp>
    <fastgltf/types.hpp>

    <SDL3/SDL.h>
  )
endif()

# ============================================================================
# Module Subdirectories
# ============================================================================
add_subdirectory(src/assets)
add_subdirectory(src/core)
add_subdirectory(src/debug)
add_subdirectory(src/filesystem)
add_subdirectory(src/platform)
# ============================================================================
# NVIDIA Aftermath Integration
# ============================================================================
option(PNKR_ENABLE_AFTERMATH "Enable NVIDIA Aftermath GPU crash debugging" ON)

message(STATUS "[Pnkr] Checking Aftermath Configuration... Enabled=${PNKR_ENABLE_AFTERMATH}")

if(PNKR_ENABLE_AFTERMATH)
  set(AFTERMATH_SDK_PATH "${CMAKE_SOURCE_DIR}/third-party/aftermath")
  
  if(EXISTS "${AFTERMATH_SDK_PATH}/include/GFSDK_Aftermath.h")
    message(STATUS "Enabling NVIDIA Aftermath support")
    target_compile_definitions(pnkr_engine PRIVATE PNKR_AFTERMATH_ENABLED=1)

    # Determine platform lib path
    if(WIN32)
      set(AFTERMATH_LIB_DIR "${AFTERMATH_SDK_PATH}/lib/x64")
      set(AFTERMATH_LIB_NAME "GFSDK_Aftermath_Lib.x64.lib")
      set(AFTERMATH_DLL_NAME "GFSDK_Aftermath_Lib.x64.dll")
    elseif(UNIX AND NOT APPLE)
      set(AFTERMATH_LIB_DIR "${AFTERMATH_SDK_PATH}/lib/x86_64")
      set(AFTERMATH_LIB_NAME "libGFSDK_Aftermath_Lib.x64.so")
    endif()

    add_library(nvidia_aftermath SHARED IMPORTED GLOBAL)
    set_target_properties(nvidia_aftermath PROPERTIES
      IMPORTED_LOCATION "${AFTERMATH_LIB_DIR}/${AFTERMATH_LIB_NAME}" # For Linux/general
      IMPORTED_IMPLIB   "${AFTERMATH_LIB_DIR}/${AFTERMATH_LIB_NAME}" # For Windows .lib
      INTERFACE_INCLUDE_DIRECTORIES "${AFTERMATH_SDK_PATH}/include"
    )

    # Copy DLL to bin folder on Windows
    if(WIN32)
      add_custom_command(TARGET pnkr_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${AFTERMATH_LIB_DIR}/${AFTERMATH_DLL_NAME}"
        "${CMAKE_BINARY_DIR}/bin/${AFTERMATH_DLL_NAME}"
        COMMENT "Copying Aftermath DLL to bin directory"
      )
    endif()
  else()
    message(FATAL_ERROR "NVIDIA Aftermath enabled (PNKR_ENABLE_AFTERMATH=ON) but SDK not found at ${AFTERMATH_SDK_PATH}.\nPlease verify the 'third-party/aftermath' directory contains the SDK headers and libraries.")
  endif()
endif()

add_subdirectory(src/rhi)
add_subdirectory(src/app)
add_subdirectory(src/renderer)

# ============================================================================
# Shader Compilation (Slang)
# ============================================================================
find_package(slang CONFIG REQUIRED)
target_link_libraries(pnkr_engine PRIVATE slang::slang)

file(GLOB_RECURSE SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders/*.slang")
foreach(SHADER ${SHADER_SOURCES})
    file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders" ${SHADER})
    configure_file(${SHADER} ${CMAKE_BINARY_DIR}/bin/assets/shaders/${REL_PATH} COPYONLY)
endforeach()

find_program(SLANGC_EXECUTABLE slangc
  HINTS
    "${CMAKE_SOURCE_DIR}/third-party/slang/bin"
    "${CMAKE_SOURCE_DIR}/thirdparty/slang/bin"
)

if(SLANGC_EXECUTABLE)
  set(SLANG_DEBUG_FLAGS "")
  set(SLANG_OPT_FLAGS "")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug: no optimizations, full debug info
    list(APPEND SLANG_DEBUG_FLAGS "-g3" "-gdwarf" "-O0" "-fspv-reflect")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    # RelWithDebInfo: optimizations WITH debug info for profiling
    list(APPEND SLANG_DEBUG_FLAGS "-g3" "-gdwarf" "-fspv-reflect")
    list(APPEND SLANG_OPT_FLAGS "-O3" "-fvk-use-dx-position-w")
  else()
    # Release: aggressive optimizations, no debug info
    list(APPEND SLANG_OPT_FLAGS "-O3" "-fvk-use-dx-position-w")
  endif()

  # Function for SPIR-V only compilation (no header generation)
  function(add_slang_target_spirv shader_source output_name entry_point shader_stage)
    string(MD5 DIR_HASH "${CMAKE_CURRENT_BINARY_DIR}")
    string(REPLACE "." "_" OUTPUT_SAFE "${output_name}")

    set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    set(SPV "${CMAKE_CURRENT_BINARY_DIR}/bin/shaders/${output_name}.spv")

    add_custom_command(
      OUTPUT ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/bin/shaders"
      COMMAND ${SLANGC_EXECUTABLE} ${SRC}
              ${SLANG_DEBUG_FLAGS} ${SLANG_OPT_FLAGS}
              -target spirv
              -entry ${entry_point}
              -stage ${shader_stage}
              -matrix-layout-column-major
              -fvk-use-scalar-layout
              -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
              -I "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/shaders"
              -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
              -o ${SPV}
      DEPENDS ${SRC}
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pnkr/renderer/gpu_shared/SlangCppBridge.h"
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pnkr/renderer/gpu_shared/CullingShared.h"
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pnkr/renderer/gpu_shared/SkinningShared.h"
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pnkr/renderer/gpu_shared/PostProcessShared.h"
              "${CMAKE_CURRENT_SOURCE_DIR}/include/pnkr/renderer/gpu_shared/SkyboxShared.h"
      COMMENT "Compiling Slang shader: ${output_name}"
    )

    set(TARGET_NAME "CompileSlang_${OUTPUT_SAFE}_${DIR_HASH}")
    add_custom_target(${TARGET_NAME} DEPENDS ${SPV})
    set(LAST_GENERATED_TARGET ${TARGET_NAME} PARENT_SCOPE)
  endfunction()

  # LineCanvas shaders
  add_slang_target_spirv("src/renderer/debug/shaders/linecanvas.slang" "linecanvas.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/debug/shaders/linecanvas.slang" "linecanvas.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  # SPIR-V only shaders
  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/culling.slang" "culling" "computeMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/skinning.slang" "skinning" "computeMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_bright" "brightMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_histogram_build" "histogramBuildMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_histogram_reduce" "histogramReduceMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_bloom" "bloomMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_bloom_down" "bloomDownsampleMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_bloom_up" "bloomUpsampleMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_adaptation" "adaptationMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "post_tonemap" "tonemapMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/post/PostProcess.slang" "fullscreen_vert" "fullscreenVert" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/skybox.slang" "skybox.vert" "skyboxMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/skybox.slang" "skybox.frag" "skyboxFrag" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/grid.slang" "grid.vert" "gridMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/grid.slang" "grid.frag" "gridFrag" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/sprites.slang" "sprite_billboard.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/sprites.slang" "sprite_billboard.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/ssao/SSAO.slang" "ssao" "ssaoMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/ssao/SSAO.slang" "ssao_depth_resolve" "depthResolveMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/ssao/SSAO.slang" "ssao_blur" "blurMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  # Environment Processing Shaders
  add_slang_target_spirv("src/renderer/shaders/environment/brdf_lut.slang" "brdf_lut" "computeMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/environment/irradiance.slang" "irradiance" "computeMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/environment/prefilter.slang" "prefilter" "computeMain" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/environment/equirectangular_to_cubemap.slang" "equirectangular_to_cubemap" "main" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  # Main renderer shaders
  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/indirect.slang" "indirect.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/indirect.slang" "indirect.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/indirect.slang" "indirect_oit.frag" "oitFragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/oit_composite.slang" "oit_composite.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/wboit_geometry.slang" "wboit_geometry.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/wboit_geometry.slang" "wboit_geometry.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/wboit_composite.slang" "wboit_composite.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/wboit_composite.slang" "wboit_composite.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/shadow.slang" "shadow.vert" "vertexMain" "vertex")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  add_slang_target_spirv("src/renderer/shaders/renderer/indirect/shadow.slang" "shadow.frag" "fragmentMain" "fragment")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

  # Physics Shaders
  add_slang_target_spirv("src/renderer/shaders/physics/cloth_simulation.slang" "cloth_simulation" "main" "compute")
  add_dependencies(pnkr_engine ${LAST_GENERATED_TARGET})

else()
  message(STATUS "slangc not found - shaders will not be compiled")
endif()

# ============================================================================
# Dependencies
# ============================================================================



target_link_libraries(pnkr_engine
  PUBLIC

    SDL3::SDL3
    Vulkan::Vulkan
)

if(WIN32)
  target_link_libraries(pnkr_engine PUBLIC Winmm)
endif()

# GLM
find_package(glm CONFIG REQUIRED)
target_link_libraries(pnkr_engine PUBLIC glm::glm)

# KTX
find_package(ktx CONFIG REQUIRED)
if(TARGET KTX::ktx)
  set(PNKR_KTX_TARGET KTX::ktx)
elseif(TARGET ktx)
  set(PNKR_KTX_TARGET ktx)
else()
  message(FATAL_ERROR "KTX target not found after find_package(ktx)")
endif()
target_link_libraries(pnkr_engine PUBLIC ${PNKR_KTX_TARGET})

# OpenCL (optional)
find_package(OpenCL QUIET)
if(OpenCL_FOUND)
  message(STATUS "OpenCL found - enabling Basis GPU acceleration")
  target_compile_definitions(pnkr_engine PRIVATE BASISU_SUPPORT_OPENCL=1)
  target_link_libraries(pnkr_engine PRIVATE OpenCL::OpenCL)
else()
  message(STATUS "OpenCL not found - Basis encoding will be CPU-only")
endif()

# VMA
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
target_link_libraries(pnkr_engine PUBLIC GPUOpen::VulkanMemoryAllocator)
# ImGui
find_package(imgui CONFIG REQUIRED)
target_link_libraries(pnkr_engine PRIVATE imgui::imgui)

# enkiTS
CPMAddPackage(
  NAME enkiTS
  GITHUB_REPOSITORY dougbinks/enkiTS
  GIT_TAG master
  OPTIONS
    "ENKITS_BUILD_EXAMPLES OFF"
    "ENKITS_INSTALL OFF"
    "ENKITS_BUILD_SHARED OFF"
)

if(TARGET enkiTS::enkiTS)
  set(PNKR_ENKITS_TARGET enkiTS::enkiTS)
elseif(TARGET EnkiTS::EnkiTS)
  set(PNKR_ENKITS_TARGET EnkiTS::EnkiTS)
elseif(TARGET enkiTS)
  set(PNKR_ENKITS_TARGET enkiTS)
endif()

# Enable Tracy in EnkiTS if TRACY_ENABLE is set
if(TRACY_ENABLE)
  target_compile_definitions(${PNKR_ENKITS_TARGET} PUBLIC TRACY_ENABLE=1)
endif()

target_link_libraries(pnkr_engine PUBLIC ${PNKR_ENKITS_TARGET})

# VTune ITT
option(PNKR_ENABLE_VTUNE "Enable Intel VTune ITT instrumentation" ON)
if(PNKR_ENABLE_VTUNE)
  CPMAddPackage(
    NAME ittapi
    GITHUB_REPOSITORY intel/ittapi
    GIT_TAG v3.26.4
  )
  if(TARGET ittapi::ittnotify)
    target_link_libraries(pnkr_engine PUBLIC ittapi::ittnotify)
    target_compile_definitions(pnkr_engine PUBLIC PNKR_ENABLE_VTUNE=1)
  endif()
endif()

# Tracy
set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy Profiler")
if(TRACY_ENABLE)
  CPMAddPackage(
    NAME tracy
    GITHUB_REPOSITORY wolfpld/tracy
    GIT_TAG v0.13.1
    OPTIONS
      "TRACY_ENABLE ON"
      "TRACY_ON_DEMAND OFF"
      "TRACY_NO_BROADCAST OFF"
      "TRACY_NO_SAMPLING OFF"
      "TRACY_NO_SYSTEM_VULKAN OFF"
      "TRACY_FIBERS ON"
  )
endif()

if(TRACY_ENABLE)
  if(NOT TARGET Tracy::TracyClient)
    find_package(tracy CONFIG REQUIRED)
  endif()
  target_link_libraries(pnkr_engine PUBLIC Tracy::TracyClient)

  # Ensure TracyClient.cpp is compiled into the engine.
  if(tracy_SOURCE_DIR)
    target_sources(pnkr_engine PRIVATE "${tracy_SOURCE_DIR}/public/TracyClient.cpp")
  elseif(TRACY_CLIENT_CPP)
    target_sources(pnkr_engine PRIVATE "${TRACY_CLIENT_CPP}")
  endif()

  target_compile_definitions(pnkr_engine PUBLIC TRACY_ENABLE=1 TRACY_FIBERS=1)
endif()

# SPIRV-Cross
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)
find_package(spirv_cross_hlsl CONFIG REQUIRED)

# Other dependencies
find_package(fastgltf CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

target_link_libraries(pnkr_engine
  PUBLIC
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-cpp
    spirv-cross-hlsl
    fastgltf::fastgltf
    meshoptimizer::meshoptimizer
    meshoptimizer::meshoptimizer
    mikktspace::mikktspace
    cpptrace::cpptrace
    nfd::nfd
    xxHash::xxhash
  )

# ============================================================================
# Copy Shaders to Output Directory
# ============================================================================
if(SLANGC_EXECUTABLE)
  add_custom_command(TARGET pnkr_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_BINARY_DIR}/bin/shaders"
      "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMENT "Copying engine SPIR-V shaders to runtime output directory"
  )
endif()

# ============================================================================
# Install Configuration
# ============================================================================
install(TARGETS pnkr_engine
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

# Code Coverage Instrumentation
pnkr_instrument_target(pnkr_engine)
