# ============================================================================
# App Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    Application.cpp
    ConsoleWindow.cpp

  PUBLIC FILE_SET headers BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include" FILES
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/app/Application.hpp"
    "${CMAKE_SOURCE_DIR}/engine/include/pnkr/app/ConsoleWindow.hpp"
)
