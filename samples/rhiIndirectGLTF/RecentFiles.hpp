#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pnkr::samples {

class RecentFiles {
public:
    explicit RecentFiles(std::string appName, size_t maxEntries = 10);

    void load();
    void save() const;

    void add(const std::filesystem::path& p);
    void clear();

    const std::vector<std::filesystem::path>& items() const { return m_items; }

    std::optional<std::filesystem::path> drawImGuiMenu(const char* menuLabel = "Recent Files");

private:
    std::filesystem::path stateFilePath() const;

    static std::string escapeJson(const std::string& s);
    static std::vector<std::string> parseJsonStringArray(const std::string& text);

private:
    std::string m_appName;
    size_t m_maxEntries = 10;
    std::vector<std::filesystem::path> m_items;
};

} // namespace pnkr::samples
