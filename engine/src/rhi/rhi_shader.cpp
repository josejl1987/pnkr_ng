#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/core/logger.hpp"
#include <algorithm>
#include <cpptrace/cpptrace.hpp>
#include <fstream>
#include <map>
#include <set>
#include <spirv_cross/spirv_cross.hpp>
#include <sstream>
#include <utility>

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
            core::Logger::RHI.error("Failed to open shader file: {}", path.string());
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

        auto entryPoints = comp.get_entry_points_and_stages();
        if (!entryPoints.empty())
        {
            m_reflection.entryPoint = entryPoints[0].name;
        }

        spirv_cross::ShaderResources res = comp.get_shader_resources();

        std::map<uint32_t, std::set<uint32_t>> processedBindings;

        auto reflectSet = [&](const auto& list, DescriptorType type)
        {
            for (const auto& r : list)
            {
                uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);

                if (processedBindings[set].contains(binding))
                {
                    core::Logger::RHI.debug("Skipping duplicate binding {} in set {}", binding, set);
                    continue;
                }
                processedBindings[set].insert(binding);

                uint32_t count = 1;
                const auto& spirvType = comp.get_type(r.type_id);

                core::Flags<DescriptorBindingFlags> flags = DescriptorBindingFlags::None;

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
                            core::Logger::RHI.warn(
                                "Runtime array '{}' using default size {}. Consider adding to bindlessOverrides configuration.",
                                resourceName, count);
                        }
                        flags = DescriptorBindingFlags::PartiallyBound | DescriptorBindingFlags::UpdateAfterBind;
                    }
                }

                if (set >= m_reflection.descriptorSets.size())
                {
                    m_reflection.descriptorSets.resize(set + 1);
                }

                m_reflection.descriptorSets[set].bindings.push_back({
                    .binding = binding,
                    .type = type,
                    .count = count,
                    .stages = m_stage,
                    .name = comp.get_name(r.id),
                    .flags = flags
                });
            }
        };

        auto reflectNonBindless = [&](const auto& list, DescriptorType type)
        {
            for (const auto& r : list)
            {
                uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);

                if (set == 1) {
                    std::string resourceName = comp.get_name(r.id);
                    core::Logger::RHI.debug("Shader uses bindless resource '{}' at set=1, binding={}",
                                        resourceName, comp.get_decoration(r.id, spv::DecorationBinding));
                }

                uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);

                if (processedBindings[set].contains(binding))
                {
                    continue;
                }
                processedBindings[set].insert(binding);

                uint32_t count = 1;
                const auto& spirvType = comp.get_type(r.type_id);

                core::Flags<DescriptorBindingFlags> flags = DescriptorBindingFlags::None;

                if (!spirvType.array.empty())
                {
                    count = spirvType.array[0];

                    if (count == 0 && m_config.enableRuntimeArrayDetection)
                    {
                      const std::string &resourceName = comp.get_name(r.id);
                      auto it = m_config.bindlessOverrides.find(resourceName);
                      if (it != m_config.bindlessOverrides.end()) {
                        count = it->second;
                      } else {
                        count = m_config.defaultBindlessSize;
                      }
                        flags = DescriptorBindingFlags::PartiallyBound | DescriptorBindingFlags::UpdateAfterBind;
                    }
                }

                if (set >= m_reflection.descriptorSets.size())
                {
                    m_reflection.descriptorSets.resize(set + 1);
                }

                m_reflection.descriptorSets[set].bindings.push_back({
                    .binding = binding,
                    .type = type,
                    .count = count,
                    .stages = m_stage,
                    .name = comp.get_name(r.id),
                    .flags = flags
                });
            }
        };

        reflectSet(res.uniform_buffers, DescriptorType::UniformBuffer);
        reflectNonBindless(res.sampled_images, DescriptorType::CombinedImageSampler);
        reflectNonBindless(res.separate_images, DescriptorType::SampledImage);
        reflectNonBindless(res.separate_samplers, DescriptorType::Sampler);
        reflectNonBindless(res.storage_images, DescriptorType::StorageImage);
        reflectNonBindless(res.storage_buffers, DescriptorType::StorageBuffer);

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
}
