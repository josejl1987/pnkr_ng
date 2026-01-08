#include "pnkr/core/RecentFilesStore.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#ifdef _WIN32
  #include <cstdlib>
#endif

namespace pnkr::core {

static std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

static void writeTextFile(const std::filesystem::path& path, const std::string& str)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      return;
    }
    file.write(str.data(), (std::streamsize)str.size());
}

RecentFilesStore::RecentFilesStore(std::string appName, size_t maxEntries)
    : m_appName(std::move(appName)), m_maxEntries(maxEntries)
{
}

std::filesystem::path RecentFilesStore::stateFilePath() const
{
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "pnkr" / (m_appName + "_recent.json");
    }
    if (const char* userprofile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(userprofile) / "AppData" / "Roaming" / "pnkr" / (m_appName + "_recent.json");
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / "pnkr" / (m_appName + "_recent.json");
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "pnkr" / (m_appName + "_recent.json");
    }
#endif
    return std::filesystem::current_path() / (m_appName + "_recent.json");
}

std::string RecentFilesStore::escapeJson(const std::string& str)
{
    std::string out;
    constexpr size_t kSafetyBuffer = 8;
    out.reserve(str.size() + kSafetyBuffer);
    for (char character : str) {
        switch (character) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            out += character;
            break;
        }
    }
    return out;
}

std::vector<std::string> RecentFilesStore::parseJsonStringArray(const std::string& text)
{
    std::vector<std::string> out;

    size_t index = 0;
    auto skipWS = [&]() {
      while (index < text.size() && std::isspace((unsigned char)text[index])) {
        ++index;
      }
    };

    skipWS();
    if (index >= text.size() || text[index] != '[') {
      return out;
    }
    ++index;

    while (true) {
        skipWS();
        if (index >= text.size()) {
          break;
        }
        if (text[index] == ']') { ++index; break; }
        if (text[index] == ',') { ++index; continue; }

        if (text[index] != '"') {
          break;
        }
        ++index;

        std::string str;
        while (index < text.size()) {
            char character = text[index++];
            if (character == '"') {
              break;
            }
            if (character == '\\' && index < text.size()) {
                char escape = text[index++];
                switch (escape) {
                case '\\': str.push_back('\\'); break;
                case '"':  str.push_back('"');  break;
                case 'n':  str.push_back('\n'); break;
                case 'r':  str.push_back('\r'); break;
                case 't':  str.push_back('\t'); break;
                default:   str.push_back(escape);    break;
                }
            } else {
                str.push_back(character);
            }
        }
        if (!str.empty()) {
          out.push_back(std::move(str));
        }

        skipWS();
        if (index < text.size() && text[index] == ',') {
          ++index;
        }
    }

    return out;
}

void RecentFilesStore::load()
{
    m_items.clear();

    const auto path = stateFilePath();
    const std::string text = readTextFile(path);
    if (text.empty()) {
      return;
    }

    auto arr = parseJsonStringArray(text);
    for (auto& str : arr) {
      if (str.empty()) {
        continue;
      }
        std::filesystem::path pathItem = str;
        if (std::filesystem::exists(pathItem)) {
          m_items.push_back(std::move(pathItem));
        }
    }

    std::vector<std::filesystem::path> dedup;
    dedup.reserve(m_items.size());
    for (const auto& pathItem : m_items) {
      if (std::ranges::find(dedup, pathItem) == dedup.end()) {
        dedup.push_back(pathItem);
      }
    }
    m_items = std::move(dedup);

    if (m_items.size() > m_maxEntries) {
      m_items.resize(m_maxEntries);
    }
}

void RecentFilesStore::save() const
{
    std::string json = "[";
    for (size_t idx = 0; idx < m_items.size(); ++idx) {
        const std::string str = m_items[idx].string();
        json += "\"";
        json += escapeJson(str);
        json += "\"";
        if (idx + 1 < m_items.size()) {
          json += ",";
        }
    }
    json += "]";

    writeTextFile(stateFilePath(), json);
}

void RecentFilesStore::add(const std::filesystem::path& path)
{
  if (path.empty()) {
    return;
  }

    std::filesystem::path norm = path;
    std::error_code errorCode;
    auto abs = std::filesystem::absolute(norm, errorCode);
    if (!errorCode) {
      norm = abs;
    }

    auto removeRange = std::ranges::remove(m_items, norm);
    m_items.erase(removeRange.begin(), removeRange.end());
    m_items.insert(m_items.begin(), norm);

    if (m_items.size() > m_maxEntries) {
      m_items.resize(m_maxEntries);
    }

    save();
}

void RecentFilesStore::clear()
{
    m_items.clear();
    save();
}

}
