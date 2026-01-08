#include "pnkr/renderer/ShaderHotReloader.hpp"
#include "pnkr/renderer/ShaderCompiler.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/logger.hpp"
#include <algorithm>

#include "pnkr/filesystem/VFS.hpp"

namespace pnkr::renderer {

void ShaderHotReloader::init(RHIRenderer* renderer) {
    m_renderer = renderer;
    ShaderCompiler::initialize();
    
    if (!filesystem::VFS::exists("/shaders")) {
        core::Logger::Render.warn("ShaderHotReloader: /shaders not mounted!");
    }
    
    core::Logger::Render.info("Shader hot-reloader initialized");
}

void ShaderHotReloader::shutdown() {
    m_watchedFiles.clear();
    m_recipes.clear();
    ShaderCompiler::shutdown();
}

void ShaderHotReloader::update(float deltaTime) {
    m_timer += deltaTime;

    if (m_timer < m_pollInterval) {
        return;
    }

    m_timer = 0.0f;

    std::unordered_set<PipelineHandle> pipelinesToRebuild;

    for (auto& [pathStr, watch] : m_watchedFiles) {
        std::error_code ec;
        auto currentTime = std::filesystem::last_write_time(pathStr, ec);

        if (ec) {
            core::Logger::Render.warn("Cannot access watched file: {}", pathStr);
            continue;
        }

        if (currentTime > watch.lastModified) {
            core::Logger::Render.info("Detected change in: {}", pathStr);
            watch.lastModified = currentTime;

            for (auto handle : watch.dependentPipelines) {
                pipelinesToRebuild.insert(handle);
            }
        }
    }

    if (!pipelinesToRebuild.empty()) {
        core::Logger::Render.info("Reloading {} pipeline(s)...", pipelinesToRebuild.size());

        m_renderer->device()->waitIdle();

        for (auto handle : pipelinesToRebuild) {
            auto it = m_recipes.find(handle);
            if (it != m_recipes.end()) {
                rebuildPipeline(handle, it->second);
            }
        }
    }
}

PipelinePtr ShaderHotReloader::createGraphicsPipeline(
    const rhi::GraphicsPipelineDescriptor& desc,
    std::span<const ShaderSourceInfo> sources
) {
    rhi::GraphicsPipelineDescriptor finalDesc = desc;
    PipelineRecipe recipe;
    recipe.gfxDesc = desc;
    recipe.isCompute = false;

    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& src = sources[i];

        auto result = ShaderCompiler::compile(src.path, src.entryPoint, src.stage);

        if (!result.success) {
            core::Logger::Render.error("Initial shader compilation failed for {}: {}",
                src.path.string(), result.error);
            return PipelinePtr{};
        }

        finalDesc.shaders[i].spirvCode = result.spirv;

        ShaderSourceInfo sourceWithDeps = src;
        sourceWithDeps.dependencies = result.dependencies;
        recipe.shaderSources.push_back(sourceWithDeps);
    }

    PipelinePtr handle = m_renderer->createGraphicsPipeline(finalDesc);

    if (!handle.isValid()) {
        core::Logger::Render.error("Failed to create graphics pipeline");
        return handle;
    }

    m_recipes[handle] = recipe;

    for (const auto& src : recipe.shaderSources) {
        registerDependencies(src, handle);
    }

    return handle;
}

PipelinePtr ShaderHotReloader::createComputePipeline(
    const rhi::ComputePipelineDescriptor& desc,
    const ShaderSourceInfo& source
) {
    auto result = ShaderCompiler::compile(source.path, source.entryPoint, rhi::ShaderStage::Compute);

    if (!result.success) {
        core::Logger::Render.error("Initial compute shader compilation failed: {}", result.error);
        return PipelinePtr{};
    }

    rhi::ComputePipelineDescriptor finalDesc = desc;
    finalDesc.shader.spirvCode = result.spirv;

    PipelinePtr handle = m_renderer->createComputePipeline(finalDesc);

    if (!handle.isValid()) {
        return handle;
    }

    PipelineRecipe recipe;
    recipe.compDesc = desc;
    recipe.isCompute = true;

    ShaderSourceInfo sourceWithDeps = source;
    sourceWithDeps.dependencies = result.dependencies;
    recipe.shaderSources.push_back(sourceWithDeps);

    m_recipes[handle] = recipe;
    registerDependencies(sourceWithDeps, handle);

    return handle;
}

void ShaderHotReloader::registerDependencies(const ShaderSourceInfo& source, PipelineHandle handle) {
    auto resolveIfVfs = [](const std::filesystem::path& p) -> std::filesystem::path {
        std::string s = p.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        if (!s.empty() && s[0] == '/') {
            auto resolved = filesystem::VFS::resolve(s);
            if (!resolved.empty()) return resolved;
        }
        return p;
    };

    std::filesystem::path resolvedMain = resolveIfVfs(source.path);
    std::string watchKey = resolvedMain.string();
    auto& mainWatch = m_watchedFiles[watchKey];
    mainWatch.dependentPipelines.insert(handle);

    std::error_code ec;
    mainWatch.lastModified = std::filesystem::last_write_time(resolvedMain, ec);
    if (ec) {
        core::Logger::Render.warn("ShaderHotReloader: Failed to get timestamp for {}: {}", resolvedMain.string(), ec.message());
    }

    for (const auto& dep : source.dependencies) {
        std::filesystem::path resolvedDep = resolveIfVfs(dep);
        std::string depKey = resolvedDep.string();
        auto& depWatch = m_watchedFiles[depKey];
        depWatch.dependentPipelines.insert(handle);
        depWatch.lastModified = std::filesystem::last_write_time(resolvedDep, ec);
    }
}

void ShaderHotReloader::rebuildPipeline(PipelineHandle handle, const PipelineRecipe& recipe) {
    if (recipe.isCompute) {
        const auto& src = recipe.shaderSources[0];
        auto result = ShaderCompiler::compile(src.path, src.entryPoint, rhi::ShaderStage::Compute);

        if (!result.success) {
            core::Logger::Render.error("Hot-reload failed for {}: {}",
                src.path.string(), result.error);
            return;
        }

        rhi::ComputePipelineDescriptor newDesc = recipe.compDesc;
        newDesc.shader.spirvCode = result.spirv;

        m_renderer->hotSwapPipeline(handle, newDesc);
        core::Logger::Render.info("✓ Hot-swapped compute pipeline {}", (uint32_t)handle.index);

    } else {
        rhi::GraphicsPipelineDescriptor newDesc = recipe.gfxDesc;
        bool allSucceeded = true;

        for (size_t i = 0; i < recipe.shaderSources.size(); ++i) {
            const auto& src = recipe.shaderSources[i];
            auto result = ShaderCompiler::compile(src.path, src.entryPoint, src.stage);

            if (!result.success) {
                core::Logger::Render.error("Hot-reload failed for {}: {}",
                    src.path.string(), result.error);
                allSucceeded = false;
                break;
            }

            newDesc.shaders[i].spirvCode = result.spirv;
            m_recipes[handle].shaderSources[i].dependencies = result.dependencies;
        }

        if (allSucceeded) {
            m_renderer->hotSwapPipeline(handle, newDesc);
            core::Logger::Render.info("✓ Hot-swapped graphics pipeline {}", (uint32_t)handle.index);
        } else {
            core::Logger::Render.warn("Keeping old pipeline due to compilation errors");
        }
    }
}

}
