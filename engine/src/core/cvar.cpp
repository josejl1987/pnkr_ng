#include "pnkr/core/cvar.hpp"
#include "pnkr/core/logger.hpp"
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace pnkr::core {

static std::unordered_map<std::string, ICVar*>& getCVarMap() {
    static std::unordered_map<std::string, ICVar*> map;
    return map;
}

void CVarSystem::registerCVar(ICVar* cvar) {
  if (cvar == nullptr) {
    return;
  }
    auto& map = getCVarMap();
    if (map.contains(cvar->name)) {
      return;
    }
    map[cvar->name] = cvar;
}

ICVar* CVarSystem::find(const std::string& name) {
    auto& map = getCVarMap();
    auto it = map.find(name);
    if (it != map.end()) {
        return it->second;
    }
    return nullptr;
}

std::unordered_map<std::string, ICVar*>& CVarSystem::getAll() {
    return getCVarMap();
}

void CVarSystem::saveToIni(const std::filesystem::path& path) {
    auto& map = getCVarMap();

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    core::Logger::info("Saving CVars to: {}", std::filesystem::absolute(path).string());
    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        core::Logger::error("Failed to open CVar file for writing: {}", path.string());
        return;
    }

    int savedCount = 0;
    for (auto const& [name, cvar] : map) {
        if (cvar->flags & CVarFlags::save) {
            f << name << "=" << cvar->toString() << "\n";
            savedCount++;
        }
    }
    core::Logger::info("Successfully saved {} CVars", savedCount);
}

void CVarSystem::loadFromIni(const std::filesystem::path& path) {
    core::Logger::info("Loading CVars from: {}", std::filesystem::absolute(path).string());
    std::ifstream f(path);
    if (!f) {
        core::Logger::warn("CVar file not found: {}", path.string());
        return;
    }

    int loadedCount = 0;
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == ';') {
        continue;
      }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
          continue;
        }

        std::string name = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (ICVar* cvar = find(name)) {
            try {
                cvar->setFromString(val);
                loadedCount++;
            } catch (...) {
                core::Logger::warn("Failed to set CVar {} from string value: {}", name, val);
            }
        }
    }
    core::Logger::info("Successfully loaded {} CVars", loadedCount);
}

CVar<std::string>::CVar(const char *name, const char *desc,
                        std::string defaultValue, CVarFlags flags,
                        OnChangeFunc onChange)
    : m_value(std::move(defaultValue)),
      m_onChange(std::move(std::move(onChange))) {
  this->name = name;
  this->description = desc;
  this->flags = flags;

  CVarSystem::registerCVar(this);
}

std::string CVar<std::string>::get() const {
    return m_value;
}

void CVar<std::string>::set(std::string val) {
    m_value = std::move(val);
    if (m_onChange) {
      m_onChange(m_value);
    }
}

std::string CVar<std::string>::toString() const {
    return m_value;
}

void CVar<std::string>::setFromString(const std::string& val) {
    set(val);
}

}
