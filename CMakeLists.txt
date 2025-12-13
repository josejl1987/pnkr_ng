cmake_minimum_required(VERSION 3.22)
project(pnkr
  VERSION 0.1.0
  LANGUAGES CXX
  DESCRIPTION "PNKR: Vulkan-based graphics engine"
)

# ============================================================================
# Standard Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler cache for faster rebuilds
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()


# --- Workaround: clang-cl (LLVM 21) exceptions ---
# clang-cl is Clang with an MSVC-like command line in CMake terms.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # Standard MSVC exception model flag.
  add_compile_options(/EHsc)  # enables C++ exception semantics in MSVC mode [web:42]

  # clang-cl-specific: make sure Clang frontend actually enables C++ exceptions.
  add_compile_options(-Xclang -fcxx-exceptions)  # workaround for /EH* not taking effect [web:4]

  # Avoid the MSVC STL being configured as "no exceptions" via macros.
  add_compile_definitions(_HAS_EXCEPTIONS=1)
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

# ============================================================================
# Dependencies
# ============================================================================

# spdlog (header-only logging)
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  CPMAddPackage(
    NAME spdlog
    VERSION 1.13.0
    GITHUB_REPOSITORY gabime/spdlog
    OPTIONS "SPDLOG_FMT_EXTERNAL=OFF"
  )
endif()

# Try to find SDL3 on the system, otherwise fetch it from GitHub
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
  CPMFindPackage(
    NAME SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
    GIT_SHALLOW TRUE
  )
endif()

# After CPMFindPackage, create the alias if it doesn't exist
if(NOT TARGET SDL3::SDL3)
  if(TARGET SDL3-shared)
    add_library(SDL3::SDL3 ALIAS SDL3-shared)
    message(STATUS "Created SDL3::SDL3 alias for SDL3-shared")
  elseif(TARGET SDL3-static)
    add_library(SDL3::SDL3 ALIAS SDL3-static)
    message(STATUS "Created SDL3::SDL3 alias for SDL3-static")
  else()
    message(FATAL_ERROR "SDL3 was fetched but no targets found (SDL3-shared or SDL3-static)")
  endif()
endif()

# Vulkan (required system package)
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



# 2. Formatting (Clang-Format)
find_program(CLANG_FORMAT_PROGRAM clang-format)
if(CLANG_FORMAT_PROGRAM)
  file(GLOB_RECURSE PNKR_SOURCES
    "engine/include/*.h" "engine/include/*.hpp"
    "engine/src/*.cpp" "engine/src/*.hpp"
    "samples/*.cpp" "samples/*.h"
  )
  add_custom_target(format
    COMMAND "${CLANG_FORMAT_PROGRAM}" -i ${PNKR_SOURCES}
    COMMENT "Formatting source code..."
  )
endif()

# ============================================================================
# Subdirectories
# ============================================================================
add_subdirectory(engine)
add_subdirectory(samples)

# ============================================================================
# Status Report
# ============================================================================
set(PNKR_SPDLOG_VERSION "unknown")
if(DEFINED spdlog_VERSION AND spdlog_VERSION)
  set(PNKR_SPDLOG_VERSION "${spdlog_VERSION}")
elseif(DEFINED SPDLOG_VERSION AND SPDLOG_VERSION)
  set(PNKR_SPDLOG_VERSION "${SPDLOG_VERSION}")
endif()

message(STATUS "")
message(STATUS "=== PNKR Configuration ===")
message(STATUS "C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "Generator:           ${CMAKE_GENERATOR}")
message(STATUS "Build Type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "spdlog found:        YES (v${PNKR_SPDLOG_VERSION})")
message(STATUS "SDL3 found:          ${SDL3_FOUND}")
message(STATUS "Vulkan found:        YES (v${Vulkan_VERSION})")
message(STATUS "")
