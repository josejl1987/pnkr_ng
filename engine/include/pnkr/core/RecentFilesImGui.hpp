#pragma once

#include "pnkr/core/RecentFilesStore.hpp"

#include <filesystem>
#include <optional>

namespace pnkr::core {

class RecentFilesImGui {
public:
    static std::optional<std::filesystem::path> drawMenu(RecentFilesStore& store,
                                                        const char* label = "Recent Files");
};

}
