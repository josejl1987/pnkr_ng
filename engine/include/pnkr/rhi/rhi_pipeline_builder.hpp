#pragma once

#include "pnkr/rhi/rhi_pipeline.hpp"
#include <vector>

#include "rhi_shader.hpp"

namespace pnkr::renderer::rhi
{
    class RHIPipelineBuilder
    {
    public:
        RHIPipelineBuilder();

        // --- Shaders (Auto-Reflection) ---
        // Clears existing shaders and merges reflection data from the provided shaders.
        RHIPipelineBuilder& setShaders(const Shader* vert, const Shader* frag, const Shader* geom = nullptr);
        RHIPipelineBuilder& setShaders(const Shader* vert, const Shader* frag,
                                       const Shader* tesc, const Shader* tese,
                                       const Shader* geom);
        RHIPipelineBuilder& setComputeShader(const Shader* comp);


        template <typename T>
        RHIPipelineBuilder& useVertexType()
        {
            m_vertexStride = sizeof(T);
            auto layout = T::getLayout();
            m_cppLayout.clear();
            for (auto& l : layout) m_cppLayout.push_back({l.m_semantic, l.m_offset, l.m_format});
            return *this;
        }


        // --- Input Assembly ---
        RHIPipelineBuilder& setTopology(PrimitiveTopology topology, bool isDynamic = false);
        RHIPipelineBuilder& setPatchControlPoints(uint32_t controlPoints);

        // --- Rasterization ---
        RHIPipelineBuilder& setPolygonMode(PolygonMode mode);
        RHIPipelineBuilder& setCullMode(CullMode mode, bool frontFaceCCW = false, bool isDynamic = false);
        RHIPipelineBuilder& setLineWidth(float width, bool isDynamic = false);
        RHIPipelineBuilder& setDepthBiasEnable(bool enable);

        // --- Depth / Stencil ---
        RHIPipelineBuilder& enableDepthTest(bool writeEnable = true, CompareOp op = CompareOp::Less, bool isDynamic = false);
        RHIPipelineBuilder& disableDepthTest(bool isDynamic = false);

        // --- Dynamic States ---
        RHIPipelineBuilder& setDynamicStates(const std::vector<DynamicState>& states);

        // --- Blending ---
        RHIPipelineBuilder& setNoBlend();
        RHIPipelineBuilder& setAlphaBlend();
        RHIPipelineBuilder& setAdditiveBlend();

        // --- Output Formats ---
        RHIPipelineBuilder& setColorFormat(Format format);
        RHIPipelineBuilder& setColorFormats(const std::vector<Format>& formats);
        RHIPipelineBuilder& setDepthFormat(Format format);

        // --- Manual Overrides (Optional) ---
        RHIPipelineBuilder& addPushConstant(ShaderStage stages, uint32_t offset, uint32_t size);
        RHIPipelineBuilder& setDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts);

        // --- Debug ---
        RHIPipelineBuilder& setName(const char* name);

        // --- Build ---
        [[nodiscard]] GraphicsPipelineDescriptor buildGraphics() const;
        [[nodiscard]] ComputePipelineDescriptor buildCompute() const;
        void setGeometryShader(Shader* get);

    private:
        GraphicsPipelineDescriptor m_gfxDesc;
        ComputePipelineDescriptor m_compDesc;
        std::vector<ReflectedInput> m_reflectedInputAttributes;

        struct CppElement
        {
            VertexSemantic semantic;
            uint32_t offset;
            Format format;
        };

        std::vector<CppElement> m_cppLayout;
        uint32_t m_vertexStride = 0;

        // Helper to merge reflection data into the target descriptor
        void mergeReflection(const ShaderReflectionData& reflection);

        // Internal storage for merged reflection before finalize
        std::vector<DescriptorSetLayout> m_mergedLayouts;
        std::vector<PushConstantRange> m_mergedPushConstants;
    };
} // namespace pnkr::renderer::rhi
