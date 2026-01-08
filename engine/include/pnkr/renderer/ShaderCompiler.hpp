#pragma once
#include <vector>
#include <string>
#include <filesystem>
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer {

struct CompileResult {
    bool success = false;
    std::vector<uint32_t> spirv;
    std::string error;
    std::vector<std::filesystem::path> dependencies;
    bool fromCache = false;
};

struct CompileOptions {
    bool debugInfo = false;
    bool optimize = true;
    bool useCache = true;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> searchPaths;
};

class ShaderCompiler {
public:
    static void initialize();
    static void shutdown();

    static void setProjectRoot(const std::filesystem::path& root);

    static CompileResult compile(
        const std::filesystem::path& sourcePath,
        const std::string& entryPoint,
        rhi::ShaderStage stage,
        const CompileOptions& options = {}
    );

    // Backward compatibility or convenience
    static CompileResult compile(
        const std::filesystem::path& sourcePath,
        const std::string& entryPoint,
        rhi::ShaderStage stage,
        const std::vector<std::string>& defines,
        const std::vector<std::filesystem::path>& searchPaths = {}
    ) {
        return compile(sourcePath, entryPoint, stage, {
            .defines = defines,
            .searchPaths = searchPaths
        });
    }

private:
    static void* s_slangSession;
    static std::filesystem::path s_projectRoot;
};

}
