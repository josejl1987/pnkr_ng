#include <unordered_set>
#include <vector>
#include <span>
#include "pnkr/core/Handle.h"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer {

struct ShaderSourceInfo {
    std::filesystem::path path;
    std::string entryPoint;
    rhi::ShaderStage stage;
    std::vector<std::filesystem::path> dependencies;
};

struct PipelineRecipe {
    rhi::GraphicsPipelineDescriptor gfxDesc;
    rhi::ComputePipelineDescriptor compDesc;
    bool isCompute = false;
    std::vector<ShaderSourceInfo> shaderSources;
};

class RHIRenderer;

class ShaderHotReloader {
public:
    void init(RHIRenderer* renderer);
    void shutdown();
    void update(float deltaTime);

    PipelinePtr createGraphicsPipeline(
        const rhi::GraphicsPipelineDescriptor& desc,
        std::span<const ShaderSourceInfo> sources
    );

    PipelinePtr createComputePipeline(
        const rhi::ComputePipelineDescriptor& desc,
        const ShaderSourceInfo& source
    );

private:
    void rebuildPipeline(PipelineHandle handle, const PipelineRecipe& recipe);
    void registerDependencies(const ShaderSourceInfo& source, PipelineHandle handle);
    static std::filesystem::path discoverProjectRoot();

    RHIRenderer* m_renderer = nullptr;

    struct WatchedFile {
        std::filesystem::file_time_type lastModified;
        std::unordered_set<PipelineHandle> dependentPipelines;
    };

    std::unordered_map<std::string, WatchedFile> m_watchedFiles;
    std::unordered_map<PipelineHandle, PipelineRecipe> m_recipes;

    float m_timer = 0.0f;
    float m_pollInterval = 0.5f;
};

}
