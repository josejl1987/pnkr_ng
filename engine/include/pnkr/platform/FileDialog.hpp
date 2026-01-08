#pragma once
#include <optional>
#include <filesystem>

namespace pnkr::platform::FileDialog
{
    std::optional<std::filesystem::path> OpenGLTFDialog();
    std::optional<std::filesystem::path> OpenImageDialog();
}
