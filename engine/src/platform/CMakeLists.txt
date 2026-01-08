# ============================================================================
# Platform Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    FileDialog.cpp
    window.cpp

  PUBLIC FILE_SET headers BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include" FILES
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/platform/FileDialog.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/platform/Input.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/platform/window.hpp"
)
