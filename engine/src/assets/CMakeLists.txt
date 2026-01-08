# ============================================================================
# Assets Module
# ============================================================================

target_sources(pnkr_engine
  PRIVATE
    AssetImporter.cpp
    TextureLoader.cpp
    GLTFParser.cpp
    GeometryProcessor.cpp

  PUBLIC
    FILE_SET headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/engine/include"
    FILES
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/AssetImporter.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/ImportedData.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/LoadProgress.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/AssetData.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/TextureLoader.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/GLTFParser.hpp"
      "${CMAKE_SOURCE_DIR}/engine/include/pnkr/assets/GeometryProcessor.hpp"
)
