#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/core/logger.hpp"
#include <sstream>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <spirv_cross/spirv_cross.hpp>
#include <algorithm>
#include <utility>
#include <cpptrace/cpptrace.hpp>

#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer::rhi
{
    static VertexSemantic parseSemanticName(std::string name)
    {
        std::ranges::transform(name, name.begin(), ::tolower);
        if (name.find("pos") != std::string::npos)
        {
            return VertexSemantic::Position;
        }
        if (name.find("color") != std::string::npos)
        {
            return VertexSemantic::Color;
        }
        if (name.find("norm") != std::string::npos)
        {
            return VertexSemantic::Normal;
        }
        if (name.find("uv0") != std::string::npos)
        {
            return VertexSemantic::TexCoord0;
        }
        if (name.find("uv1") != std::string::npos)
        {
            return VertexSemantic::TexCoord1;
        }
        if (name.find("uv") != std::string::npos ||
            name.find("coord") != std::string::npos)
        {
            return VertexSemantic::TexCoord;
        }
        if (name.find("tangent") != std::string::npos)
        {
            return VertexSemantic::Tangent;
        }
        if (name.find("weight") != std::string::npos)
        {
            return VertexSemantic::Weights;
        }
        if (name.find("bone") != std::string::npos)
        {
            return VertexSemantic::BoneIds;
        }
        return VertexSemantic::Unknown;
    }

    Shader::Shader(ShaderStage stage, const std::vector<uint32_t>& spirvCode, ReflectionConfig config)
        : m_stage(stage)
          , m_code(spirvCode)
          , m_config(std::move(config))
    {
        reflect();
    }

    std::unique_ptr<Shader> Shader::load(ShaderStage stage, const std::filesystem::path& path,
                                         const ReflectionConfig& config)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            core::Logger::error("Failed to open shader file: {}", path.string());
            throw cpptrace::runtime_error("Shader file not found");
        }

        size_t fileSize = (size_t)file.tellg();
        if (fileSize % 4 != 0)
        {
            throw cpptrace::runtime_error("Invalid SPIR-V size");
        }

        std::vector<uint32_t> buffer(fileSize / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

        return std::make_unique<Shader>(stage, buffer, config);
    }

    void Shader::reflect()
    {
        spirv_cross::Compiler comp(m_code);
        spirv_cross::ShaderResources res = comp.get_shader_resources();

        // Track which bindings we've already added to avoid duplicates
        std::map<uint32_t, std::set<uint32_t>> processedBindings;

        // 1. Reflect Descriptors (UBOs, Textures, etc.)
        auto reflectSet = [&](const auto& list, DescriptorType type)
        {
            for (const auto& r : list)
            {
                uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);

                // Skip if this binding was already processed
                if (processedBindings[set].contains(binding))
                {
                    core::Logger::debug("Skipping duplicate binding {} in set {}", binding, set);
                    continue;
                }
                processedBindings[set].insert(binding);

                // Determine Array Size
                uint32_t count = 1;
                const auto& spirvType = comp.get_type(r.type_id);

                if (!spirvType.array.empty())
                {
                    count = spirvType.array[0];

                    // SPIR-V reports 0 for runtime arrays (e.g., textures[])
                    if (count == 0 && m_config.enableRuntimeArrayDetection)
                    {
                        std::string resourceName = comp.get_name(r.id);

                        // Check if this resource has an explicit configuration
                        auto it = m_config.bindlessOverrides.find(resourceName);
                        if (it != m_config.bindlessOverrides.end())
                        {
                            count = it->second;
                        }
                        else
                        {
                            // Use default size for unconfigured runtime arrays
                            count = m_config.defaultBindlessSize;
                            core::Logger::warn(
                                "Runtime array '{}' using default size {}. Consider adding to bindlessOverrides configuration.",
                                resourceName, count);
                        }
                    }
                }

                if (set >= m_reflection.descriptorSets.size())
                {
                    m_reflection.descriptorSets.resize(set + 1);
                }

                m_reflection.descriptorSets[set].bindings.push_back({
                    .binding = binding,
                    .type = type,
                    .count = count, // Use calculated count, not hardcoded 1
                    .stages = m_stage
                });
            }
        };

        // Reflect non-bindless resources only
        auto reflectNonBindless = [&](const auto& list, DescriptorType type)
        {
            for (const auto& r : list)
            {
                uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                
                // NOTE: We do NOT skip set 1 anymore. The device still provides the actual layout for set 1,
                // but reflecting it allows validation and ensures descriptorSets[] is dense by set index.
                if (set == 1) {
                    std::string resourceName = comp.get_name(r.id);
                    core::Logger::debug("Shader uses bindless resource '{}' at set=1, binding={}",
                                        resourceName, comp.get_decoration(r.id, spv::DecorationBinding));
                }

                // Process non-bindless resources
                uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);
                
                if (processedBindings[set].contains(binding))
                {
                    continue;
                }
                processedBindings[set].insert(binding);
                
                uint32_t count = 1;
                const auto& spirvType = comp.get_type(r.type_id);
                
                if (!spirvType.array.empty())
                {
                    count = spirvType.array[0];
                    
                    if (count == 0 && m_config.enableRuntimeArrayDetection)
                    {
                        std::string resourceName = comp.get_name(r.id);
                        auto it = m_config.bindlessOverrides.find(resourceName);
                        if (it != m_config.bindlessOverrides.end())
                        {
                            count = it->second;
                        }
                        else
                        {
                            count = m_config.defaultBindlessSize;
                        }
                    }
                }
                
                if (set >= m_reflection.descriptorSets.size())
                {
                    m_reflection.descriptorSets.resize(set + 1);
                }
                
                m_reflection.descriptorSets[set].bindings.push_back({.binding = binding, .type = type, .count = count, .stages = m_stage});
            }
        };

        reflectSet(res.uniform_buffers, DescriptorType::UniformBuffer);
        reflectNonBindless(res.sampled_images, DescriptorType::CombinedImageSampler);
        reflectNonBindless(res.separate_images, DescriptorType::SampledImage);
        reflectNonBindless(res.separate_samplers, DescriptorType::Sampler);
        reflectNonBindless(res.storage_images, DescriptorType::StorageImage);
        reflectNonBindless(res.storage_buffers, DescriptorType::StorageBuffer);

        // 2. Reflect Push Constants
        for (const auto& resource : res.push_constant_buffers)
        {
            const auto& type = comp.get_type(resource.base_type_id);


            auto size = (uint32_t)comp.get_declared_struct_size(type);

            m_reflection.pushConstants.push_back({
                .stages = m_stage,
                .offset = 0,
                .size = size
            });
        }

        // 3. Reflect Vertex Inputs (Bridge)
        if (m_stage == ShaderStage::Vertex)
        {
            for (const auto& input : res.stage_inputs)
            {
                m_reflection.inputAttributes.push_back({
                    .location = comp.get_decoration(input.id, spv::DecorationLocation),
                    .semantic = parseSemanticName(comp.get_name(input.id))
                });
            }
        }
    }
} // namespace pnkr::renderer::rhi
