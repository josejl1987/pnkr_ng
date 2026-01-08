#include "pnkr/renderer/ShaderCompiler.hpp"
#include "pnkr/renderer/ShaderCache.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/filesystem/VFS.hpp"
#include <slang.h>
#include <cstring>

namespace pnkr::renderer {

void* ShaderCompiler::s_slangSession = nullptr;
std::filesystem::path ShaderCompiler::s_projectRoot;

void ShaderCompiler::initialize() {
    s_slangSession = spCreateSession();
    
    // Initialize shader cache in 'pnkr_shader_cache' directory
    ShaderCache::initialize("pnkr_shader_cache");

    core::Logger::Render.info("Slang shader compiler initialized");
}

void ShaderCompiler::shutdown() {
    ShaderCache::shutdown();
    if (s_slangSession) {
        spDestroySession(static_cast<SlangSession*>(s_slangSession));
        s_slangSession = nullptr;
    }
}

void ShaderCompiler::setProjectRoot(const std::filesystem::path& root) {
    s_projectRoot = root;
    core::Logger::Render.info("ShaderCompiler: Project root set to {}", s_projectRoot.string());
}

CompileResult ShaderCompiler::compile(
    const std::filesystem::path& sourcePath,
    const std::string& entryPoint,
    rhi::ShaderStage stage,
    const CompileOptions& options
) {
    CompileResult result;

    if (!s_slangSession) {
        result.error = "Slang session not initialized";
        return result;
    }

    // Resolve path for hashing and loading
    std::filesystem::path resolvedPath = sourcePath;
    std::string pathStr = sourcePath.string();
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    if (pathStr.length() > 0 && pathStr[0] == '/') {
        auto p = filesystem::VFS::resolve(pathStr);
        if (!p.empty()) {
            resolvedPath = p;
        }
    }

    // 1. Check Cache
    ShaderCacheKey cacheKey{
        .sourcePath = resolvedPath,
        .entryPoint = entryPoint,
        .stage = stage,
        .defines = options.defines,
        .debugInfo = options.debugInfo,
        .optimize = options.optimize
    };

    if (options.useCache) {
        auto cached = ShaderCache::load(cacheKey);
        if (cached) {
            result.success = true;
            result.spirv = std::move(cached->spirv);
            result.dependencies = std::move(cached->dependencies);
            result.fromCache = true;
            core::Logger::Render.info("Loaded shader from cache: {} [{}]", sourcePath.string(), entryPoint);
            return result;
        }
    }

    // 2. Compile if cache miss
    SlangCompileRequest* request = spCreateCompileRequest(static_cast<SlangSession*>(s_slangSession));

    int targetIndex = spAddCodeGenTarget(request, SLANG_SPIRV);
    spSetTargetProfile(request, targetIndex, spFindProfile(static_cast<SlangSession*>(s_slangSession), "spirv_1_6"));
    spSetTargetFlags(request, targetIndex, SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY);

    // Set Debug Info and Optimization levels
    if (options.debugInfo) {
        spSetDebugInfoLevel(request, SLANG_DEBUG_INFO_LEVEL_STANDARD);
    }
    
    if (options.optimize) {
        spSetOptimizationLevel(request, SLANG_OPTIMIZATION_LEVEL_MAXIMAL);
    } else {
        spSetOptimizationLevel(request, SLANG_OPTIMIZATION_LEVEL_NONE);
    }

    spAddSearchPath(request, "assets/shaders");
    spAddSearchPath(request, "include");
    
    auto vfsShaders = filesystem::VFS::resolve("/shaders");
    if (!vfsShaders.empty()) {
        spAddSearchPath(request, vfsShaders.string().c_str());
    }
    
    auto vfsInclude = filesystem::VFS::resolve("/include");
    if (!vfsInclude.empty()) {
        spAddSearchPath(request, vfsInclude.string().c_str());
    }
    
    for (const auto& path : options.searchPaths) {
        spAddSearchPath(request, path.string().c_str());
    }

    spSetMatrixLayoutMode(request, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);

    for (const auto& define : options.defines) {
        size_t eqPos = define.find('=');
        if (eqPos != std::string::npos) {
            std::string name = define.substr(0, eqPos);
            std::string value = define.substr(eqPos + 1);
            spAddPreprocessorDefine(request, name.c_str(), value.c_str());
        } else {
            spAddPreprocessorDefine(request, define.c_str(), "1");
        }
    }

    int translationUnitIndex = spAddTranslationUnit(request, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(request, translationUnitIndex, resolvedPath.string().c_str());

    SlangStage slangStage;
    switch(stage) {
        case rhi::ShaderStage::Vertex:   slangStage = SLANG_STAGE_VERTEX; break;
        case rhi::ShaderStage::Fragment: slangStage = SLANG_STAGE_FRAGMENT; break;
        case rhi::ShaderStage::Compute:  slangStage = SLANG_STAGE_COMPUTE; break;
        case rhi::ShaderStage::TessControl: slangStage = SLANG_STAGE_HULL; break;
        case rhi::ShaderStage::TessEval: slangStage = SLANG_STAGE_DOMAIN; break;
        case rhi::ShaderStage::Geometry: slangStage = SLANG_STAGE_GEOMETRY; break;
        default:
            result.error = "Unsupported shader stage";
            spDestroyCompileRequest(request);
            return result;
    }

    int entryPointIndex = spAddEntryPoint(request, translationUnitIndex, entryPoint.c_str(), slangStage);

    SlangResult compileResult = spCompile(request);

    if (SLANG_FAILED(compileResult)) {
        const char* diagnostics = spGetDiagnosticOutput(request);
        result.error = diagnostics ? diagnostics : "Unknown compilation error";
        result.success = false;

        core::Logger::Render.error("Shader compilation failed for {}:\n{}",
            sourcePath.string(), result.error);
    } else {
        size_t codeSize = 0;
        const void* code = spGetEntryPointCode(request, entryPointIndex, &codeSize);

        if (code && codeSize > 0) {
            result.spirv.resize(codeSize / sizeof(uint32_t));
            memcpy(result.spirv.data(), code, codeSize);
            result.success = true;
        } else {
            result.error = "Failed to retrieve compiled SPIR-V code";
            result.success = false;
        }

        int depCount = spGetDependencyFileCount(request);
        for (int i = 0; i < depCount; ++i) {
            const char* depPath = spGetDependencyFilePath(request, i);
            if (depPath) {
                result.dependencies.push_back(depPath);
            }
        }

        core::Logger::Render.info("Compiled {} -> {} bytes SPIR-V, {} dependencies",
            sourcePath.string(), codeSize, result.dependencies.size());

        // 3. Store in Cache
        if (result.success && options.useCache) {
            ShaderCacheEntry cacheEntry{
                .spirv = result.spirv,
                .dependencies = result.dependencies,
                .sourceHash = ShaderCache::computeFileHash(resolvedPath)
            };
            ShaderCache::store(cacheKey, cacheEntry);
        }
    }

    spDestroyCompileRequest(request);

    return result;
}

}
