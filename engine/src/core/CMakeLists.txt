# ============================================================================
# Core Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    ECS.cpp
    cvar.cpp
    implementations.cpp
    logger.cpp
    RecentFiles.cpp
    RecentFilesImGui.cpp
    RecentFilesStore.cpp
    TaskSystem.cpp

  PUBLIC FILE_SET headers BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include" FILES
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/cvar.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/ECS.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/FramePacer.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/Handle.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/LinearAllocator.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/Pool.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/RecentFiles.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/RecentFilesImGui.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/RecentFilesStore.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/System.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/TaskSystem.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/ThreadSafeQueue.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/Timer.h"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/cache.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/common.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/logger.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/core/profiler.hpp"
)
