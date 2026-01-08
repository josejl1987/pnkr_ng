#include "pnkr/filesystem/VFS.hpp"
#include "pnkr/core/logger.hpp"
#include <unordered_map>
#include <fstream>
#include <algorithm>
#include <mutex>

namespace pnkr::filesystem {

namespace {
    struct MountPoint {
        std::string virtualPrefix;
        std::filesystem::path physicalPath;
    };

    std::vector<MountPoint> s_mountPoints;
    std::mutex s_mutex;

    std::string normalizeVirtual(const std::string& path) {
        if (path.empty()) return "/";
        std::string result = path;
        std::replace(result.begin(), result.end(), '\\', '/');
        if (result.front() != '/') result.insert(0, "/");
        if (result.length() > 1 && result.back() == '/') result.pop_back();
        return result;
    }
}

void VFS::mount(const std::string& virtualPath, const std::filesystem::path& physicalPath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    std::string vPath = normalizeVirtual(virtualPath);
    
    if (!std::filesystem::exists(physicalPath)) {
        core::Logger::Platform.warn("VFS: Physical path does not exist: {}", physicalPath.string());
    }

    s_mountPoints.push_back({vPath, std::filesystem::absolute(physicalPath)});
    
    std::sort(s_mountPoints.begin(), s_mountPoints.end(), 
        [](const MountPoint& a, const MountPoint& b) {
            return a.virtualPrefix.length() > b.virtualPrefix.length();
        });

    core::Logger::Platform.info("VFS: Mounted '{}' -> '{}'", vPath, physicalPath.string());
}

std::filesystem::path VFS::resolve(const std::string& virtualPath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    std::string vPath = normalizeVirtual(virtualPath);

    for (const auto& mp : s_mountPoints) {
        if (vPath.find(mp.virtualPrefix) == 0) {
            std::string remainder = vPath.substr(mp.virtualPrefix.length());
            
            if (!remainder.empty() && remainder.front() != '/') {
                if (mp.virtualPrefix != "/") {
                    continue; 
                }
            }

            auto result = mp.physicalPath;
            if (!remainder.empty()) {
                if (remainder.front() == '/') remainder = remainder.substr(1);
                result /= remainder;
            }
            return result;
        }
    }

    return {};
}

bool VFS::exists(const std::string& virtualPath) {
    auto path = resolve(virtualPath);
    return !path.empty() && std::filesystem::exists(path);
}

std::optional<std::string> VFS::readText(const std::string& virtualPath) {
    auto path = resolve(virtualPath);
    if (path.empty() || !std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::optional<std::vector<uint8_t>> VFS::readBytes(const std::string& virtualPath) {
    auto path = resolve(virtualPath);
    if (path.empty() || !std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }

    return std::nullopt;
}

void VFS::clear() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_mountPoints.clear();
}

} // namespace pnkr::filesystem
