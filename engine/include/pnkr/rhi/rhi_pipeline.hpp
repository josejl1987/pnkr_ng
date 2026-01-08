#pragma once

#include "rhi_types.hpp"
#include "rhi_descriptor.hpp"
#include <vector>
#include <string>

namespace pnkr::renderer::rhi
{
    class RHIDescriptorSetLayout;

    struct ShaderModuleDescriptor
    {
        ShaderStage stage;
        std::vector<uint32_t> spirvCode;
        std::string entryPoint = "main";
    };

    struct RasterizationState
    {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        bool frontFaceCCW = true;
        float lineWidth = 1.0f;
        bool depthBiasEnable = false;
    };

    struct DepthStencilState
    {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool stencilTestEnable = false;
    };

    struct BlendAttachment
    {
        bool blendEnable = false;
        BlendFactor srcColorBlendFactor = BlendFactor::One;
        BlendFactor dstColorBlendFactor = BlendFactor::Zero;
        BlendOp colorBlendOp = BlendOp::Add;
        BlendFactor srcAlphaBlendFactor = BlendFactor::One;
        BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
        BlendOp alphaBlendOp = BlendOp::Add;
    };

    struct BlendState
    {
        std::vector<BlendAttachment> attachments;
    };

    struct MultisampleState
    {
        uint32_t rasterizationSamples = 1;
        bool sampleShadingEnable = false;
        float minSampleShading = 0.0f;
    };

    struct PushConstantRange
    {
        ShaderStageFlags stages;
        uint32_t offset;
        uint32_t size;
    };

    struct GraphicsPipelineDescriptor
    {

        std::vector<ShaderModuleDescriptor> shaders;

        std::vector<VertexInputBinding> vertexBindings;
        std::vector<VertexInputAttribute> vertexAttributes;

        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        uint32_t patchControlPoints = 3;

        RasterizationState rasterization;

        DepthStencilState depthStencil;

        BlendState blend;

        MultisampleState multisample;

        std::vector<Format> colorFormats;
        Format depthFormat = Format::Undefined;

        std::vector<DescriptorSetLayout> descriptorSets;
        std::vector<PushConstantRange> pushConstants;

        std::vector<DynamicState> dynamicStates;

        std::string debugName;
    };

    struct ComputePipelineDescriptor
    {
        ShaderModuleDescriptor shader;
        std::vector<DescriptorSetLayout> descriptorSets;
        std::vector<PushConstantRange> pushConstants;
        std::string debugName;
    };

    class RHIPipeline
    {
    public:
        virtual ~RHIPipeline() = default;

        virtual PipelineBindPoint bindPoint() const = 0;
        virtual void* nativeHandle() const = 0;
        virtual RHIDescriptorSetLayout* descriptorSetLayout(uint32_t setIndex) const = 0;
        virtual uint32_t descriptorSetLayoutCount() const = 0;
    };

}
