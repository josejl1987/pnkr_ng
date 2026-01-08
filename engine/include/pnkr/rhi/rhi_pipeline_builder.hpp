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
            for (auto& l : layout) m_cppLayout.push_back({l.semantic, l.offset, l.format});
            return *this;
        }

        RHIPipelineBuilder& setTopology(PrimitiveTopology topology, bool isDynamic = false);
        RHIPipelineBuilder& setPatchControlPoints(uint32_t controlPoints);

        RHIPipelineBuilder& setPolygonMode(PolygonMode mode);
        RHIPipelineBuilder& setCullMode(CullMode mode, bool frontFaceCCW = false, bool isDynamic = false);
        RHIPipelineBuilder& setLineWidth(float width, bool isDynamic = false);
        RHIPipelineBuilder& setDepthBiasEnable(bool enable);

        RHIPipelineBuilder& setMultisampling(uint32_t sampleCount, bool sampleShading = false, float minSampleShading = 0.0f);

        RHIPipelineBuilder& enableDepthTest(bool writeEnable = true, CompareOp op = CompareOp::Less, bool isDynamic = false);
        RHIPipelineBuilder& disableDepthTest(bool isDynamic = false);

        RHIPipelineBuilder& setDynamicStates(const std::vector<DynamicState>& states);

        RHIPipelineBuilder& setNoBlend();
        RHIPipelineBuilder& setAlphaBlend();
        RHIPipelineBuilder& setAdditiveBlend();
        RHIPipelineBuilder& setBlend(uint32_t attachment, BlendOp op, BlendFactor src, BlendFactor dst);
        RHIPipelineBuilder& setBlend(uint32_t attachment, BlendOp op, BlendFactor src, BlendFactor dst, BlendOp alphaOp, BlendFactor srcAlpha, BlendFactor dstAlpha);

        RHIPipelineBuilder& setColorFormat(Format format);
        RHIPipelineBuilder& setColorFormats(const std::vector<Format>& formats);
        RHIPipelineBuilder& setDepthFormat(Format format);

        RHIPipelineBuilder& addPushConstant(ShaderStageFlags stages, uint32_t offset, uint32_t size);
        RHIPipelineBuilder& setDescriptorSetLayouts(const std::vector<DescriptorSetLayout>& layouts);

        RHIPipelineBuilder& setName(const std::string& name);

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

        void mergeReflection(const ShaderReflectionData& reflection);

        std::vector<DescriptorSetLayout> m_mergedLayouts;
        std::vector<PushConstantRange> m_mergedPushConstants;
    };
}
