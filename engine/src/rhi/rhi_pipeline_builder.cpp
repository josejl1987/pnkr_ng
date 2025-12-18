#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include <algorithm>

namespace pnkr::renderer::rhi {

    RHIPipelineBuilder::RHIPipelineBuilder() {
        // Defaults for Graphics
        m_gfxDesc.topology = PrimitiveTopology::TriangleList;
        m_gfxDesc.rasterization.polygonMode = PolygonMode::Fill;
        m_gfxDesc.rasterization.cullMode = CullMode::Back;
        m_gfxDesc.rasterization.frontFaceCCW = true;
        m_gfxDesc.rasterization.lineWidth = 1.0f;
        m_gfxDesc.depthStencil.depthTestEnable = true;
        m_gfxDesc.depthStencil.depthWriteEnable = true;
        m_gfxDesc.depthStencil.depthCompareOp = CompareOp::Less;

        setNoBlend(); // Default blend
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setShaders(const Shader* vert, const Shader* frag) {
        m_gfxDesc.shaders.clear();
        m_mergedLayouts.clear();
        m_mergedPushConstants.clear();

        if (vert) {
            ShaderModuleDescriptor sm{};
            sm.stage = ShaderStage::Vertex;
            sm.spirvCode = vert->code();
            sm.entryPoint = vert->reflection().entryPoint;
            m_gfxDesc.shaders.push_back(sm);
            mergeReflection(vert->reflection());
            m_reflectedInputAttributes = vert->reflection().inputAttributes;
        }

        if (frag) {
            ShaderModuleDescriptor sm{};
            sm.stage = ShaderStage::Fragment;
            sm.spirvCode = frag->code();
            sm.entryPoint = frag->reflection().entryPoint;
            m_gfxDesc.shaders.push_back(sm);
            mergeReflection(frag->reflection());
        }
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setComputeShader(const Shader* comp) {
        m_mergedLayouts.clear();
        m_mergedPushConstants.clear();

        if (comp) {
            m_compDesc.shader.stage = ShaderStage::Compute;
            m_compDesc.shader.spirvCode = comp->code();
            m_compDesc.shader.entryPoint = comp->reflection().entryPoint;
            mergeReflection(comp->reflection());
        }
        return *this;
    }

    void RHIPipelineBuilder::mergeReflection(const ShaderReflectionData& reflection) {
        // --- 1. Robust Push Constant Merging ---
        for (const auto& incomingPC : reflection.pushConstants) {
            if (m_mergedPushConstants.empty()) {
                m_mergedPushConstants.push_back(incomingPC);
            } else {
                auto& existing = m_mergedPushConstants[0];
                // Union of stages: e.g. Vertex | Fragment
                existing.stages = existing.stages | incomingPC.stages;
                // Max of sizes: ensure the layout is big enough for the largest user
                existing.size = std::max(existing.size, incomingPC.size);
            }
        }

        // --- 2. Merge Descriptor Sets (Logic remains the same) ---
        if (reflection.descriptorSets.size() > m_mergedLayouts.size()) {
            m_mergedLayouts.resize(reflection.descriptorSets.size());
        }
        for (size_t i = 0; i < reflection.descriptorSets.size(); ++i) {
            const auto& incomingSet = reflection.descriptorSets[i];
            auto& targetSet = m_mergedLayouts[i];
            for (const auto& binding : incomingSet.bindings) {
                bool found = false;
                for (auto& existingBinding : targetSet.bindings) {
                    if (existingBinding.binding == binding.binding) {
                        existingBinding.stages = existingBinding.stages | binding.stages;
                        found = true;
                        break;
                    }
                }
                if (!found) targetSet.bindings.push_back(binding);
            }
        }
    }

    // --- State Setters (Boilerplate) ---


    RHIPipelineBuilder& RHIPipelineBuilder::setTopology(PrimitiveTopology topology) {
        m_gfxDesc.topology = topology;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setPolygonMode(PolygonMode mode) {
        m_gfxDesc.rasterization.polygonMode = mode;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setCullMode(CullMode mode, bool frontFaceCCW) {
        m_gfxDesc.rasterization.cullMode = mode;
        m_gfxDesc.rasterization.frontFaceCCW = frontFaceCCW;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setLineWidth(float width) {
        m_gfxDesc.rasterization.lineWidth = width;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::enableDepthTest(bool writeEnable, CompareOp op) {
        m_gfxDesc.depthStencil.depthTestEnable = true;
        m_gfxDesc.depthStencil.depthWriteEnable = writeEnable;
        m_gfxDesc.depthStencil.depthCompareOp = op;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::disableDepthTest() {
        m_gfxDesc.depthStencil.depthTestEnable = false;
        m_gfxDesc.depthStencil.depthWriteEnable = false;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setNoBlend() {
        if (m_gfxDesc.blend.attachments.empty()) m_gfxDesc.blend.attachments.resize(1);
        for (auto& att : m_gfxDesc.blend.attachments) {
            att.blendEnable = false;
            att.srcColorBlendFactor = BlendFactor::One;
            att.dstColorBlendFactor = BlendFactor::Zero;
        }
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setAlphaBlend() {
        if (m_gfxDesc.blend.attachments.empty()) m_gfxDesc.blend.attachments.resize(1);
        for (auto& att : m_gfxDesc.blend.attachments) {
            att.blendEnable = true;
            att.srcColorBlendFactor = BlendFactor::SrcAlpha;
            att.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
            att.colorBlendOp = BlendOp::Add;
            att.srcAlphaBlendFactor = BlendFactor::One;
            att.dstAlphaBlendFactor = BlendFactor::Zero;
            att.alphaBlendOp = BlendOp::Add;
        }
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setAdditiveBlend() {
        if (m_gfxDesc.blend.attachments.empty()) m_gfxDesc.blend.attachments.resize(1);
        for (auto& att : m_gfxDesc.blend.attachments) {
            att.blendEnable = true;
            att.srcColorBlendFactor = BlendFactor::One;
            att.dstColorBlendFactor = BlendFactor::One;
            att.colorBlendOp = BlendOp::Add;
            att.srcAlphaBlendFactor = BlendFactor::One;
            att.dstAlphaBlendFactor = BlendFactor::Zero;
            att.alphaBlendOp = BlendOp::Add;
        }
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setColorFormat(Format format) {
        m_gfxDesc.colorFormats = { format };
        if (m_gfxDesc.blend.attachments.size() != 1) m_gfxDesc.blend.attachments.resize(1);
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setColorFormats(const std::vector<Format>& formats) {
        m_gfxDesc.colorFormats = formats;
        m_gfxDesc.blend.attachments.resize(formats.size()); // ensure blend states exist
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setDepthFormat(Format format) {
        m_gfxDesc.depthFormat = format;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::addPushConstant(ShaderStage stages, uint32_t offset, uint32_t size) {
        // Manual override adds to the merged list
        m_mergedPushConstants.push_back({ stages, offset, size });
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts) {
        m_mergedLayouts = layouts;
        return *this;
    }

    RHIPipelineBuilder& RHIPipelineBuilder::setName(const char* name) {
        m_gfxDesc.debugName = name;
        m_compDesc.debugName = name;
        return *this;
    }

    GraphicsPipelineDescriptor RHIPipelineBuilder::buildGraphics() const {
        GraphicsPipelineDescriptor desc = m_gfxDesc;
        desc.descriptorSets = m_mergedLayouts;
        desc.pushConstants = m_mergedPushConstants;

        for (const auto& pc : desc.pushConstants) {
            core::Logger::info("[Builder] Pipeline PushConstant: StageFlags={}, Size={}, Offset={}",
                               (uint32_t)pc.stages, pc.size, pc.offset);
        }



        if (m_vertexStride > 0) {
            desc.vertexBindings.push_back({0, m_vertexStride, VertexInputRate::Vertex});

            // Match Reflected Inputs (Shader) to Cpp Layout (Vertex Struct)
            for (const auto& shaderIn : m_reflectedInputAttributes) {
                auto it = std::find_if(m_cppLayout.begin(), m_cppLayout.end(),
                                       [&](const auto& cpp) { return cpp.semantic == shaderIn.semantic; });

                if (it != m_cppLayout.end()) {
                    desc.vertexAttributes.push_back({
                        shaderIn.location,
                        0,
                        it->format,
                        it->offset,
                        it->semantic
                    });
                } else {
                    core::Logger::warn("Vertex shader requires semantic {} at location {}, but C++ Vertex struct does not provide it.",
                                       (int)shaderIn.semantic, shaderIn.location);
                }
            }
        }
        return desc;
    }

    ComputePipelineDescriptor RHIPipelineBuilder::buildCompute() const {
        ComputePipelineDescriptor desc = m_compDesc;
        desc.descriptorSets = m_mergedLayouts;
        desc.pushConstants = m_mergedPushConstants;
        return desc;
    }

} // namespace pnkr::renderer::rhi