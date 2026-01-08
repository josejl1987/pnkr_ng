#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace pnkr::filesystem {

class VFS {
public:
    /**
     * @brief Mounts a physical path to a virtual mount point.
     * @param virtualPath The virtual path prefix (e.g., "/shaders").
     * @param physicalPath The physical path on disk.
     */
    static void mount(const std::string& virtualPath, const std::filesystem::path& physicalPath);

    /**
     * @brief Resolves a virtual path to a physical path.
     * @param virtualPath The virtual path to resolve.
     * @return The physical path if found, or an empty path if not.
     */
    static std::filesystem::path resolve(const std::string& virtualPath);

    /**
     * @brief Checks if a file exists at the given virtual path.
     */
    static bool exists(const std::string& virtualPath);

    /**
     * @brief Reads a text file from the VFS.
     * @return The file content as a string, or nullopt if failed.
     */
    static std::optional<std::string> readText(const std::string& virtualPath);

    /**
     * @brief Reads a binary file from the VFS.
     * @return The file content as a vector of bytes, or nullopt if failed.
     */
    static std::optional<std::vector<uint8_t>> readBytes(const std::string& virtualPath);

    /**
     * @brief Clears all mount points.
     */
    static void clear();
};

} // namespace pnkr::filesystem
