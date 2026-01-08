#pragma once

#include <filesystem>
#include "pnkr/core/RecentFilesStore.hpp"

#include <optional>
#include <string>

namespace pnkr::core {

class RecentFiles {
public:
    explicit RecentFiles(std::string appName, size_t maxEntries = 10);

    void load() { m_store.load(); }
    void save() const { m_store.save(); }

    void add(const std::filesystem::path& p) { m_store.add(p); }
    void clear() { m_store.clear(); }

    const std::vector<std::filesystem::path>& items() const { return m_store.items(); }

    std::optional<std::filesystem::path> drawImGuiMenu(const char* menuLabel = "Recent Files");

private:
    RecentFilesStore m_store;
};

}
