cmake_minimum_required(VERSION 3.25)
project(PNKR_Engine
        VERSION 0.1.0
        LANGUAGES CXX C
        DESCRIPTION "PNKR: Vulkan-based graphics engine"
)

# FINAL FIX: Disable ccache, use lld + PCH
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC)
    set(CMAKE_CXX_COMPILER_LAUNCHER "")  # Remove ccache
    set(CMAKE_LINKER_TYPE LLD)           # Use LLVM lld (faster)
    message(STATUS "Using clang-cl + lld + PCH (no ccache)")
endif()

# ============================================================================
# Standard Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# --- Workaround: clang-cl (LLVM 21) exceptions ---
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  add_compile_options(/EHsc)
  add_compile_options(-Xclang -fcxx-exceptions)
  add_compile_definitions(_HAS_EXCEPTIONS=1)
endif()

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_definitions(NOMINMAX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(PNKR_DEBUG)
  message(STATUS "Defined PNKR_DEBUG for Debug build type.")
endif()

# Prefer faster linkers when available
if(NOT MSVC)
  find_program(MOLD_LINKER mold)
  if(MOLD_LINKER)
    add_link_options("-fuse-ld=mold")
    message(STATUS "Using mold linker")
  else()
    find_program(LLD_LINKER ld.lld)
    if(LLD_LINKER)
      add_link_options("-fuse-ld=lld")
      message(STATUS "Using lld linker")
    endif()
  endif()
endif()

# Compiler Cache
if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC))
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
      set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
  endif()
endif()

# Generator-specific: Prefer Ninja
if(NOT DEFINED CMAKE_GENERATOR)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    set(CMAKE_GENERATOR "Ninja Multi-Config" CACHE STRING "Build generator")
  endif()
endif()

# ============================================================================
# Dependencies Setup
# ============================================================================
include(FetchContent)
set(CPM_USE_LOCAL_PACKAGES ON)

add_subdirectory(third-party)

# Include cmake modules
include(cmake/ShaderTools.cmake)

# ============================================================================
# Find Dependencies
# ============================================================================

# quill
find_package(quill CONFIG REQUIRED)


# cpptrace
# cpptrace
find_package(cpptrace CONFIG QUIET)
if(NOT cpptrace_FOUND)
  CPMAddPackage(
    NAME cpptrace
    GITHUB_REPOSITORY jeremy-rifkin/cpptrace
    VERSION 0.7.3
  )
endif()

# ImGuizmo (part of imgui)
CPMAddPackage(
        NAME ImGuizmo
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG master
)

# SDL3
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  CPMFindPackage(
          NAME SDL3
          GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
          GIT_TAG release-3.2.0
          GIT_SHALLOW TRUE
  )
endif()

if(NOT TARGET SDL3::SDL3)
  if(TARGET SDL3-shared)
    add_library(SDL3::SDL3 ALIAS SDL3-shared)
    message(STATUS "Created SDL3::SDL3 alias for SDL3-shared")
  elseif(TARGET SDL3-static)
    add_library(SDL3::SDL3 ALIAS SDL3-static)
    message(STATUS "Created SDL3::SDL3 alias for SDL3-static")
  else()
    message(FATAL_ERROR "SDL3 was fetched but no targets found")
  endif()
endif()

# Vulkan (required)
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
  message(FATAL_ERROR
          "Vulkan SDK not found. Install from https://vulkan.lunarg.com\n"
          "Linux: sudo apt install vulkan-sdk\n"
          "macOS: brew install vulkan-sdk\n"
          "Windows: https://vulkan.lunarg.com/sdk/home"
  )
endif()

# ============================================================================
# Code Quality & Style
# ============================================================================

# Clang-Format target
find_program(CLANG_FORMAT_PROGRAM clang-format)
if(CLANG_FORMAT_PROGRAM)
  # Explicit file list - no GLOB
  set(PNKR_SOURCES
          "engine/include/pnkr/app/Application.hpp"
          "engine/include/pnkr/assets/AssetImporter.hpp"
          "engine/include/pnkr/assets/ImportedData.hpp"
          "engine/include/pnkr/assets/LoadProgress.hpp"
          "engine/include/pnkr/core/ECS.hpp"
          "engine/include/pnkr/core/FramePacer.hpp"
          "engine/include/pnkr/core/Handle.h"
          "engine/include/pnkr/core/LinearAllocator.hpp"
          "engine/include/pnkr/core/Pool.hpp"
          "engine/include/pnkr/core/RecentFiles.hpp"
          "engine/include/pnkr/core/RecentFilesImGui.hpp"
          "engine/include/pnkr/core/RecentFilesStore.hpp"
          "engine/include/pnkr/core/System.hpp"
          "engine/include/pnkr/core/TaskSystem.hpp"
          "engine/include/pnkr/core/ThreadSafeQueue.hpp"
          "engine/include/pnkr/core/Timer.h"
          "engine/include/pnkr/core/cache.hpp"
          "engine/include/pnkr/core/common.hpp"
          "engine/include/pnkr/core/logger.hpp"
          "engine/include/pnkr/core/profiler.hpp"
          "engine/include/pnkr/engine.hpp"
          "engine/include/pnkr/platform/FileDialog.hpp"
          "engine/include/pnkr/platform/Input.hpp"
          "engine/include/pnkr/platform/window.hpp"
          # ... add all other headers explicitly
  )

  # Add all cpp files
  list(APPEND PNKR_SOURCES
          "engine/src/assets/AssetImporter.cpp"
          "engine/src/core/RecentFiles.cpp"
          "engine/src/core/RecentFilesImGui.cpp"
          "engine/src/core/RecentFilesStore.cpp"
          "engine/src/core/TaskSystem.cpp"
          "engine/src/core/implementations.cpp"
          "engine/src/core/logger.cpp"
          "engine/src/platform/FileDialog.cpp"
          "engine/src/platform/window.cpp"
          # ... add all other cpp files explicitly
  )

  add_custom_target(format
          COMMAND "${CLANG_FORMAT_PROGRAM}" -i ${PNKR_SOURCES}
          COMMENT "Formatting source code..."
  )
endif()

# ============================================================================
# Build Tree
# ============================================================================
add_subdirectory(engine)
add_subdirectory(samples)
enable_testing()
add_subdirectory(tests)

# ============================================================================
# Status Report
# ============================================================================
set(PNKR_QUILL_VERSION "unknown")
if(DEFINED quill_VERSION AND quill_VERSION)
  set(PNKR_QUILL_VERSION "${quill_VERSION}")
endif()

message(STATUS "")
message(STATUS "=== PNKR Configuration ===")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "Generator:           ${CMAKE_GENERATOR}")
message(STATUS "Build Type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "quill found:         YES (v${PNKR_QUILL_VERSION})")

message(STATUS "SDL3 found:          ${SDL3_FOUND}")
message(STATUS "Vulkan found:        YES (v${Vulkan_VERSION})")
message(STATUS "")
