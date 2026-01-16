
# CPMAddPackage(
#     NAME doctest
#     GITHUB_REPOSITORY doctest/doctest
#     GIT_TAG v2.4.11
# )

find_package(doctest CONFIG REQUIRED)
include(doctest)

add_executable(pnkr_tests
    doctest_main.cpp
    assets/texture_loader_test.cpp
    renderer/Test_ResourceStateMachine.cpp
    renderer/Test_ResourceRequestManager.cpp
    renderer/Test_AsyncLoader.cpp
    renderer/Test_NullRHI.cpp
    renderer/Test_RHIResourceManager.cpp
)

target_include_directories(pnkr_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

set_target_properties(pnkr_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Link against the engine library. 
target_link_libraries(pnkr_tests PRIVATE pnkr_engine doctest::doctest quill::quill)

doctest_discover_tests(pnkr_tests PROPERTIES LABELS "Null")

# ============================================================================
# Vulkan Tests with Lavapipe
# ============================================================================

add_executable(pnkr_vulkan_tests
    doctest_main.cpp
    rhi/VulkanTestContext.cpp
    rhi/Test_VulkanRHI.cpp
    rhi/Test_VulkanCompute.cpp
    rhi/Test_VulkanPipelines.cpp
    rhi/Test_VulkanSynchronization.cpp
    rhi/Test_VulkanBindless.cpp
)

set_target_properties(pnkr_vulkan_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(pnkr_vulkan_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/engine/src
)

target_link_libraries(pnkr_vulkan_tests
    PRIVATE
    pnkr_engine
    doctest::doctest
    quill::quill
    Vulkan::Vulkan
)

target_compile_definitions(pnkr_vulkan_tests PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

# ============================================================================
# Test Shader Compilation
# ============================================================================

if(NOT SLANGC_EXECUTABLE)
  find_program(SLANGC_EXECUTABLE slangc
    HINTS
      "${CMAKE_SOURCE_DIR}/third-party/slang/bin"
      "${CMAKE_SOURCE_DIR}/thirdparty/slang/bin"
  )
endif()

if(SLANGC_EXECUTABLE)
  set(SLANG_DEBUG_FLAGS "")
  set(SLANG_OPT_FLAGS "")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND SLANG_DEBUG_FLAGS "-g3" "-gdwarf" "-O0" "-fspv-reflect")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    list(APPEND SLANG_DEBUG_FLAGS "-g3" "-gdwarf" "-fspv-reflect")
    list(APPEND SLANG_OPT_FLAGS "-O3" "-fvk-use-dx-position-w")
  else()
    list(APPEND SLANG_OPT_FLAGS "-O3" "-fvk-use-dx-position-w")
  endif()

  function(add_test_slang_target_spirv shader_source output_name entry_point shader_stage)
    string(MD5 DIR_HASH "${CMAKE_CURRENT_BINARY_DIR}")
    string(REPLACE "." "_" OUTPUT_SAFE "${output_name}")

    set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/${shader_source}")
    set(SPV "${CMAKE_BINARY_DIR}/bin/shaders/${output_name}.spv")

    add_custom_command(
      OUTPUT ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/shaders"
      COMMAND ${SLANGC_EXECUTABLE} ${SRC}
              ${SLANG_DEBUG_FLAGS} ${SLANG_OPT_FLAGS}
              -target spirv
              -entry ${entry_point}
              -stage ${shader_stage}
              -matrix-layout-column-major
              -fvk-use-scalar-layout
              -I "${CMAKE_SOURCE_DIR}/engine/include"
              -I "${CMAKE_SOURCE_DIR}/engine/src/renderer/shaders"
              -I "${CMAKE_SOURCE_DIR}/engine/shaders"
              -o ${SPV}
      DEPENDS ${SRC}
              "${CMAKE_SOURCE_DIR}/engine/include/pnkr/renderer/gpu_shared/SlangCppBridge.h"
              "${CMAKE_SOURCE_DIR}/engine/src/renderer/shaders/shared/Bindless.slang"
      COMMENT "Compiling Slang shader: ${output_name}"
    )

    set(TARGET_NAME "CompileSlangTest_${OUTPUT_SAFE}_${DIR_HASH}")
    add_custom_target(${TARGET_NAME} DEPENDS ${SPV})
    set(LAST_TEST_SHADER_TARGET ${TARGET_NAME} PARENT_SCOPE)
  endfunction()

  add_test_slang_target_spirv("shaders/test_compute.slang" "test_compute" "computeMain" "compute")
  add_dependencies(pnkr_vulkan_tests ${LAST_TEST_SHADER_TARGET})

  add_test_slang_target_spirv("shaders/test_vertex.slang" "test_vertex" "vertexMain" "vertex")
  add_dependencies(pnkr_vulkan_tests ${LAST_TEST_SHADER_TARGET})

  add_test_slang_target_spirv("shaders/test_fragment.slang" "test_fragment" "fragmentMain" "fragment")
  add_dependencies(pnkr_vulkan_tests ${LAST_TEST_SHADER_TARGET})

  add_test_slang_target_spirv("shaders/test_bindless.slang" "test_bindless" "bindlessTestMain" "compute")
  add_dependencies(pnkr_vulkan_tests ${LAST_TEST_SHADER_TARGET})
else()
  message(STATUS "slangc not found - Vulkan test shaders will not be compiled")
endif()

set(PNKR_VK_ICD_FILENAMES "" CACHE STRING "Path to Vulkan ICD JSON to use for Vulkan tests")
set(PNKR_VK_LAYER_PATH "" CACHE STRING "Path to Vulkan explicit layer directory for Vulkan tests")

if(PNKR_VK_ICD_FILENAMES)
  set(PNKR_VULKAN_TEST_ENV "VK_ICD_FILENAMES=${PNKR_VK_ICD_FILENAMES}")
endif()
if(PNKR_VK_LAYER_PATH)
  list(APPEND PNKR_VULKAN_TEST_ENV "VK_LAYER_PATH=${PNKR_VK_LAYER_PATH}")
endif()

if(PNKR_VULKAN_TEST_ENV)
  doctest_discover_tests(pnkr_vulkan_tests
    PROPERTIES LABELS "Vulkan" ENVIRONMENT "${PNKR_VULKAN_TEST_ENV}")
else()
  doctest_discover_tests(pnkr_vulkan_tests PROPERTIES LABELS "Vulkan")
endif()

# Code Coverage
pnkr_instrument_target(pnkr_tests)
pnkr_add_coverage_report(pnkr_tests)

pnkr_instrument_target(pnkr_vulkan_tests)
pnkr_add_coverage_report(pnkr_vulkan_tests)
