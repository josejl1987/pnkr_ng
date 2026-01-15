#pragma once
#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/assets/LoadProgress.hpp"
#include <filesystem>
#include <memory>

namespace pnkr::assets {

    class AssetImporter {
    public:
        static std::unique_ptr<ImportedModel> loadGLTF(const std::filesystem::path& path, LoadProgress* progress = nullptr, uint32_t maxTextureSize = 4096);
    };

}
