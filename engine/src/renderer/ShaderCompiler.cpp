#include "pnkr/renderer/ShaderCompiler.hpp"
#include "pnkr/core/logger.hpp"
#include <slang.h>
#include <cstring>

namespace pnkr::renderer {

void* ShaderCompiler::s_slangSession = nullptr;

void ShaderCompiler::initialize() {
    s_slangSession = spCreateSession();

    SlangSessionDesc sessionDesc = {};
    sessionDesc.structureSize = sizeof(SlangSessionDesc);

    SlangPreprocessorMacroDesc macros[1] = {};
    sessionDesc.preprocessorMacroCount = 0;
    sessionDesc.preprocessorMacros = macros;

    core::Logger::Render.info("Slang shader compiler initialized");
}

void ShaderCompiler::shutdown() {
    if (s_slangSession) {
        spDestroySession(static_cast<SlangSession*>(s_slangSession));
        s_slangSession = nullptr;
    }
}

CompileResult ShaderCompiler::compile(
    const std::filesystem::path& sourcePath,
    const std::string& entryPoint,
    rhi::ShaderStage stage,
    const std::vector<std::string>& defines,
    const std::vector<std::filesystem::path>& searchPaths
) {
    CompileResult result;

    if (!s_slangSession) {
        result.error = "Slang session not initialized";
        return result;
    }

    SlangCompileRequest* request = spCreateCompileRequest(static_cast<SlangSession*>(s_slangSession));

    int targetIndex = spAddCodeGenTarget(request, SLANG_SPIRV);
    spSetTargetProfile(request, targetIndex, spFindProfile(static_cast<SlangSession*>(s_slangSession), "spirv_1_6"));
    spSetTargetFlags(request, targetIndex, SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY);

    spAddSearchPath(request, "assets/shaders");
    spAddSearchPath(request, "include");
    for (const auto& path : searchPaths) {
        spAddSearchPath(request, path.string().c_str());
    }

    spSetMatrixLayoutMode(request, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);

    for (const auto& define : defines) {
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
    spAddTranslationUnitSourceFile(request, translationUnitIndex, sourcePath.string().c_str());

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
    }

    spDestroyCompileRequest(request);

    return result;
}

}
