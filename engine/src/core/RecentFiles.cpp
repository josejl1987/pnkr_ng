#include "pnkr/core/RecentFiles.hpp"
#include "pnkr/core/RecentFilesImGui.hpp"

namespace pnkr::core {

RecentFiles::RecentFiles(std::string appName, size_t maxEntries)
    : m_store(std::move(appName), maxEntries)
{
}

std::optional<std::filesystem::path> RecentFiles::drawImGuiMenu(const char* menuLabel)
{
    return RecentFilesImGui::drawMenu(m_store, menuLabel);
}

}
