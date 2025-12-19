#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp" // For DescriptorSetLayout definitions
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>

namespace pnkr::renderer::rhi {

    struct ReflectedInput {
        uint32_t location;
        VertexSemantic semantic;
    };

    struct ShaderReflectionData {
        std::vector<DescriptorSetLayout> descriptorSets;
        std::vector<PushConstantRange> pushConstants;
        std::vector<ReflectedInput> inputAttributes; // Only for Vertex Stage
        std::string entryPoint = "main";
    };

    struct ReflectionConfig {
        // Map specific resource names to their descriptor counts
        std::unordered_map<std::string, uint32_t> bindlessOverrides = {
            {"bindlessTextures", 100000},
            {"bindlessStorageBuffers", 100000},
            {"bindlessStorageImages", 10000}
        };

        // Default size for runtime arrays not explicitly configured
        uint32_t defaultBindlessSize = 1000;

        // Enable/disable runtime array detection
        bool enableRuntimeArrayDetection = true;
    };

    class Shader {
    public:
        Shader(ShaderStage stage, const std::vector<uint32_t>& spirvCode, ReflectionConfig  config = {});
        static std::unique_ptr<Shader> load(ShaderStage stage, const std::filesystem::path& path, const ReflectionConfig& config = {});

        [[nodiscard]] ShaderStage stage() const { return m_stage; }
        [[nodiscard]] const std::vector<uint32_t>& code() const { return m_code; }
        [[nodiscard]] const ShaderReflectionData& reflection() const { return m_reflection; }

    private:
        ShaderStage m_stage;
        std::vector<uint32_t> m_code;
        ShaderReflectionData m_reflection;
        ReflectionConfig m_config;
        void reflect();
    };
}