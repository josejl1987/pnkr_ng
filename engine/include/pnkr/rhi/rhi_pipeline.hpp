#pragma once

#include "rhi_types.hpp"
#include "rhi_descriptor.hpp"
#include <vector>
#include <string>



namespace pnkr::renderer::rhi
{
    class RHIDescriptorSetLayout;
    // Shader module

    struct ShaderModuleDescriptor
    {
        ShaderStage stage;
        std::vector<uint32_t> spirvCode;  // SPIR-V bytecode
        std::string entryPoint = "main";
    };

    // Rasterization state
    struct RasterizationState
    {
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        bool frontFaceCCW = true;
        float lineWidth = 1.0f;
    };

    // Depth/stencil state
    struct DepthStencilState
    {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool stencilTestEnable = false;
    };

    // Blend state
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

    // Push constant range
    struct PushConstantRange
    {
        ShaderStage stages;
        uint32_t offset;
        uint32_t size;
    };

    // Descriptor set layout binding
    struct DescriptorBinding
    {
        uint32_t binding;
        DescriptorType type;
        uint32_t count = 1;
        ShaderStage stages;
    };

    struct DescriptorSetLayout
    {
        std::vector<DescriptorBinding> bindings;
    };

    // Graphics pipeline descriptor
    struct GraphicsPipelineDescriptor
    {
        // Shaders
        std::vector<ShaderModuleDescriptor> shaders;

        // Vertex input
        std::vector<VertexInputBinding> vertexBindings;
        std::vector<VertexInputAttribute> vertexAttributes;

        // Input assembly
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;

        // Rasterization
        RasterizationState rasterization;

        // Depth/stencil
        DepthStencilState depthStencil;

        // Blending
        BlendState blend;

        // Render target formats
        std::vector<Format> colorFormats;
        Format depthFormat = Format::Undefined;

        // Resource layouts
        std::vector<DescriptorSetLayout> descriptorSets;
        std::vector<PushConstantRange> pushConstants;

        const char* debugName = nullptr;
    };

    // Compute pipeline descriptor
    struct ComputePipelineDescriptor
    {
        ShaderModuleDescriptor shader;
        std::vector<DescriptorSetLayout> descriptorSets;
        std::vector<PushConstantRange> pushConstants;
        const char* debugName = nullptr;
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

} // namespace pnkr::renderer::rhi
