#include "pnkr/core/RecentFiles.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#ifdef _WIN32
  #include <cstdlib>
#endif

#include <imgui.h>

namespace pnkr::core {

static std::string readTextFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void writeTextFile(const std::filesystem::path& p, const std::string& s)
{
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(s.data(), (std::streamsize)s.size());
}

RecentFiles::RecentFiles(std::string appName, size_t maxEntries)
    : m_appName(std::move(appName)), m_maxEntries(maxEntries)
{
}

std::filesystem::path RecentFiles::stateFilePath() const
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

std::string RecentFiles::escapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::vector<std::string> RecentFiles::parseJsonStringArray(const std::string& text)
{
    std::vector<std::string> out;

    size_t i = 0;
    auto skipWS = [&]() {
        while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    };

    skipWS();
    if (i >= text.size() || text[i] != '[') return out;
    ++i;

    while (true) {
        skipWS();
        if (i >= text.size()) break;
        if (text[i] == ']') { ++i; break; }
        if (text[i] == ',') { ++i; continue; }

        if (text[i] != '"') break;
        ++i;

        std::string s;
        while (i < text.size()) {
            char c = text[i++];
            if (c == '"') break;
            if (c == '\\' && i < text.size()) {
                char e = text[i++];
                switch (e) {
                case '\\': s.push_back('\\'); break;
                case '"':  s.push_back('"');  break;
                case 'n':  s.push_back('\n'); break;
                case 'r':  s.push_back('\r'); break;
                case 't':  s.push_back('\t'); break;
                default:   s.push_back(e);    break;
                }
            } else {
                s.push_back(c);
            }
        }
        if (!s.empty())
            out.push_back(std::move(s));

        skipWS();
        if (i < text.size() && text[i] == ',') ++i;
    }

    return out;
}

void RecentFiles::load()
{
    m_items.clear();

    const auto path = stateFilePath();
    const std::string text = readTextFile(path);
    if (text.empty()) return;

    auto arr = parseJsonStringArray(text);
    for (auto& s : arr) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        if (std::filesystem::exists(p))
            m_items.push_back(std::move(p));
    }

    std::vector<std::filesystem::path> dedup;
    dedup.reserve(m_items.size());
    for (const auto& p : m_items) {
        if (std::find(dedup.begin(), dedup.end(), p) == dedup.end())
            dedup.push_back(p);
    }
    m_items = std::move(dedup);

    if (m_items.size() > m_maxEntries)
        m_items.resize(m_maxEntries);
}

void RecentFiles::save() const
{
    std::string json = "[";
    for (size_t idx = 0; idx < m_items.size(); ++idx) {
        const std::string s = m_items[idx].string();
        json += "\"";
        json += escapeJson(s);
        json += "\"";
        if (idx + 1 < m_items.size()) json += ",";
    }
    json += "]";

    writeTextFile(stateFilePath(), json);
}

void RecentFiles::add(const std::filesystem::path& p)
{
    if (p.empty()) return;

    std::filesystem::path norm = p;
    std::error_code ec;
    auto abs = std::filesystem::absolute(norm, ec);
    if (!ec) norm = abs;

    m_items.erase(std::remove(m_items.begin(), m_items.end(), norm), m_items.end());
    m_items.insert(m_items.begin(), norm);

    if (m_items.size() > m_maxEntries)
        m_items.resize(m_maxEntries);

    save();
}

void RecentFiles::clear()
{
    m_items.clear();
    save();
}

std::optional<std::filesystem::path> RecentFiles::drawImGuiMenu(const char* menuLabel)
{
    std::optional<std::filesystem::path> chosen;

    if (ImGui::BeginMenu(menuLabel))
    {
        if (m_items.empty()) {
            ImGui::MenuItem("(empty)", nullptr, false, false);
        } else {
            for (size_t i = 0; i < m_items.size(); ++i) {
                const auto& p = m_items[i];
                const bool exists = std::filesystem::exists(p);
                std::string label = std::to_string(i + 1) + "  " + p.filename().string();

                if (ImGui::MenuItem(label.c_str(), nullptr, false, exists)) {
                    chosen = p;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", p.string().c_str());
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear")) {
                clear();
            }
        }

        ImGui::EndMenu();
    }

    return chosen;
}

} // namespace pnkr::core
