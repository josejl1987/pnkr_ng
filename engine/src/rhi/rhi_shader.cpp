#include "pnkr/rhi/rhi_shader.hpp"

#include "pnkr/core/logger.hpp"
#include <fstream>
#include <spirv_cross/spirv_cross.hpp>
#include <algorithm>

#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::renderer::rhi {


    static VertexSemantic parseSemanticName(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find("pos") != std::string::npos)      return VertexSemantic::Position;
        if (name.find("color") != std::string::npos)    return VertexSemantic::Color;
        if (name.find("norm") != std::string::npos)     return VertexSemantic::Normal;
        if (name.find("uv") != std::string::npos ||
            name.find("coord") != std::string::npos)    return VertexSemantic::TexCoord;
        if (name.find("tangent") != std::string::npos)  return VertexSemantic::Tangent;
        if (name.find("weight") != std::string::npos)   return VertexSemantic::Weights;
        if (name.find("bone") != std::string::npos)     return VertexSemantic::BoneIds;
        return VertexSemantic::Unknown;
    }

    Shader::Shader(ShaderStage stage, const std::vector<uint32_t>& spirvCode, const ReflectionConfig& config)
        : m_stage(stage)
        , m_code(spirvCode)
        , m_config(config)
    {
        reflect();
    }

    std::unique_ptr<Shader> Shader::load(ShaderStage stage, const std::filesystem::path& path, const ReflectionConfig& config) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            core::Logger::error("Failed to open shader file: {}", path.string());
            throw std::runtime_error("Shader file not found");
        }

        size_t fileSize = (size_t)file.tellg();
        if (fileSize % 4 != 0) {
             throw std::runtime_error("Invalid SPIR-V size");
        }

        std::vector<uint32_t> buffer(fileSize / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

        return std::make_unique<Shader>(stage, buffer, config);
    }

    void Shader::reflect() {
        spirv_cross::Compiler comp(m_code);
        spirv_cross::ShaderResources res = comp.get_shader_resources();

        // 1. Reflect Descriptors (UBOs, Textures, etc.)
        auto reflectSet = [&](const auto& list, DescriptorType type) {
            for (const auto& r : list) {
                uint32_t set = comp.get_decoration(r.id, spv::DecorationDescriptorSet);
                uint32_t binding = comp.get_decoration(r.id, spv::DecorationBinding);

                // Determine Array Size
                uint32_t count = 1;
                const auto& spirvType = comp.get_type(r.type_id);

                if (!spirvType.array.empty()) {
                    count = spirvType.array[0];

                    // SPIR-V reports 0 for runtime arrays (e.g., textures[])
                    if (count == 0 && m_config.enableRuntimeArrayDetection) {
                        std::string resourceName = comp.get_name(r.id);

                        // Check if this resource has an explicit configuration
                        auto it = m_config.bindlessOverrides.find(resourceName);
                        if (it != m_config.bindlessOverrides.end()) {
                            count = it->second;
                        } else {
                            // Use default size for unconfigured runtime arrays
                            count = m_config.defaultBindlessSize;
                            core::Logger::warn("Runtime array '{}' using default size {}. Consider adding to bindlessOverrides configuration.",
                                              resourceName, count);
                        }
                    }
                }

                if (set >= m_reflection.descriptorSets.size()) m_reflection.descriptorSets.resize(set + 1);

                m_reflection.descriptorSets[set].bindings.push_back({
                    binding,
                    type,
                    count, // Use calculated count, not hardcoded 1
                    m_stage
                });
            }
        };

        reflectSet(res.uniform_buffers, DescriptorType::UniformBuffer);
        reflectSet(res.sampled_images, DescriptorType::CombinedImageSampler);
        reflectSet(res.storage_images, DescriptorType::StorageImage);
        reflectSet(res.storage_buffers, DescriptorType::StorageBuffer);

        // 2. Reflect Push Constants
        for (const auto& resource : res.push_constant_buffers) {
            const auto& type = comp.get_type(resource.base_type_id);


            uint32_t size = (uint32_t)comp.get_declared_struct_size(type);

            m_reflection.pushConstants.push_back({
                m_stage,
                0,
                size
            });
        }

        // 3. Reflect Vertex Inputs (Bridge)
        if (m_stage == ShaderStage::Vertex) {
            for (const auto& input : res.stage_inputs) {
                m_reflection.inputAttributes.push_back({
                    comp.get_decoration(input.id, spv::DecorationLocation),
                    parseSemanticName(comp.get_name(input.id))
                });
            }
        }
    }
} // namespace pnkr::renderer::rhi