#pragma once
#include <memory>
#include <filesystem>
#include "pnkr/assets/AssetData.hpp"

namespace pnkr::assets {

    std::unique_ptr<TextureAsset> loadTextureFromDisk(
        const std::filesystem::path& path,
        bool forceSrgb = false);

}
