#include "pnkr/rhi/vulkan/vulkan_utils.hpp"
#include "pnkr/core/common.hpp"

#include <stdexcept>
#include <vk_mem_alloc.h>

using namespace pnkr::util;

namespace pnkr::renderer::rhi::vulkan
{
    vk::Format VulkanUtils::toVkFormat(Format format)
    {
        switch (format)
        {
        case Format::Undefined: return vk::Format::eUndefined;

        // Color formats
        case Format::R8_UNORM: return vk::Format::eR8Unorm;
        case Format::R8G8_UNORM: return vk::Format::eR8G8Unorm;
        case Format::R8G8B8_UNORM: return vk::Format::eR8G8B8Unorm;
        case Format::R8G8B8A8_UNORM: return vk::Format::eR8G8B8A8Unorm;
        case Format::R8G8B8A8_SRGB: return vk::Format::eR8G8B8A8Srgb;
        case Format::B8G8R8A8_UNORM: return vk::Format::eB8G8R8A8Unorm;
        case Format::B8G8R8A8_SRGB: return vk::Format::eB8G8R8A8Srgb;

        case Format::R16_SFLOAT: return vk::Format::eR16Sfloat;
        case Format::R16G16_SFLOAT: return vk::Format::eR16G16Sfloat;
        case Format::R16G16B16A16_SFLOAT: return vk::Format::eR16G16B16A16Sfloat;

        case Format::R32_SFLOAT: return vk::Format::eR32Sfloat;
        case Format::R32G32_SFLOAT: return vk::Format::eR32G32Sfloat;
        case Format::R32G32B32_SFLOAT: return vk::Format::eR32G32B32Sfloat;
        case Format::R32G32B32A32_SFLOAT: return vk::Format::eR32G32B32A32Sfloat;

        // Depth/stencil
        case Format::D16_UNORM: return vk::Format::eD16Unorm;
        case Format::D32_SFLOAT: return vk::Format::eD32Sfloat;
        case Format::D24_UNORM_S8_UINT: return vk::Format::eD24UnormS8Uint;
        case Format::D32_SFLOAT_S8_UINT: return vk::Format::eD32SfloatS8Uint;

        // Compressed
        case Format::BC1_RGB_UNORM: return vk::Format::eBc1RgbUnormBlock;
        case Format::BC1_RGB_SRGB: return vk::Format::eBc1RgbSrgbBlock;
        case Format::BC3_UNORM: return vk::Format::eBc3UnormBlock;
        case Format::BC3_SRGB: return vk::Format::eBc3SrgbBlock;
        case Format::BC7_UNORM: return vk::Format::eBc7UnormBlock;
        case Format::BC7_SRGB: return vk::Format::eBc7SrgbBlock;

        default: return vk::Format::eUndefined;
        }
    }

    Format VulkanUtils::fromVkFormat(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eUndefined: return Format::Undefined;

        case vk::Format::eR8Unorm: return Format::R8_UNORM;
        case vk::Format::eR8G8Unorm: return Format::R8G8_UNORM;
        case vk::Format::eR8G8B8Unorm: return Format::R8G8B8_UNORM;
        case vk::Format::eR8G8B8A8Unorm: return Format::R8G8B8A8_UNORM;
        case vk::Format::eR8G8B8A8Srgb: return Format::R8G8B8A8_SRGB;
        case vk::Format::eB8G8R8A8Unorm: return Format::B8G8R8A8_UNORM;
        case vk::Format::eB8G8R8A8Srgb: return Format::B8G8R8A8_SRGB;

        case vk::Format::eR16Sfloat: return Format::R16_SFLOAT;
        case vk::Format::eR16G16Sfloat: return Format::R16G16_SFLOAT;
        case vk::Format::eR16G16B16A16Sfloat: return Format::R16G16B16A16_SFLOAT;

        case vk::Format::eR32Sfloat: return Format::R32_SFLOAT;
        case vk::Format::eR32G32Sfloat: return Format::R32G32_SFLOAT;
        case vk::Format::eR32G32B32Sfloat: return Format::R32G32B32_SFLOAT;
        case vk::Format::eR32G32B32A32Sfloat: return Format::R32G32B32A32_SFLOAT;

        case vk::Format::eD16Unorm: return Format::D16_UNORM;
        case vk::Format::eD32Sfloat: return Format::D32_SFLOAT;
        case vk::Format::eD24UnormS8Uint: return Format::D24_UNORM_S8_UINT;
        case vk::Format::eD32SfloatS8Uint: return Format::D32_SFLOAT_S8_UINT;

        default: return Format::Undefined;
        }
    }

    vk::BufferUsageFlags VulkanUtils::toVkBufferUsage(BufferUsage usage)
    {
        vk::BufferUsageFlags flags{};

        if (hasFlag(usage, BufferUsage::TransferSrc)) {
            flags |= vk::BufferUsageFlagBits::eTransferSrc;
        }
        if (hasFlag(usage, BufferUsage::TransferDst)) {
            flags |= vk::BufferUsageFlagBits::eTransferDst;
        }
        if (hasFlag(usage, BufferUsage::UniformBuffer)) {
            flags |= vk::BufferUsageFlagBits::eUniformBuffer;
        }
        if (hasFlag(usage, BufferUsage::StorageBuffer)) {
            flags |= vk::BufferUsageFlagBits::eStorageBuffer;
        }
        if (hasFlag(usage, BufferUsage::IndexBuffer)) {
            flags |= vk::BufferUsageFlagBits::eIndexBuffer;
        }
        if (hasFlag(usage, BufferUsage::VertexBuffer)) {
            flags |= vk::BufferUsageFlagBits::eVertexBuffer;
        }
        if (hasFlag(usage, BufferUsage::IndirectBuffer)) {
            flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
        }
        if (hasFlag(usage, BufferUsage::ShaderDeviceAddress)) {
            flags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        }

        return flags;
    }

    BufferUsage VulkanUtils::fromVkBufferUsage(vk::BufferUsageFlags flags)
    {
        BufferUsage usage = BufferUsage::None;

        if (flags & vk::BufferUsageFlagBits::eTransferSrc) {
            usage |= BufferUsage::TransferSrc;
        }
        if (flags & vk::BufferUsageFlagBits::eTransferDst) {
            usage |= BufferUsage::TransferDst;
        }
        if (flags & vk::BufferUsageFlagBits::eUniformBuffer) {
            usage |= BufferUsage::UniformBuffer;
        }
        if (flags & vk::BufferUsageFlagBits::eStorageBuffer) {
            usage |= BufferUsage::StorageBuffer;
        }
        if (flags & vk::BufferUsageFlagBits::eIndexBuffer) {
            usage |= BufferUsage::IndexBuffer;
        }
        if (flags & vk::BufferUsageFlagBits::eVertexBuffer) {
            usage |= BufferUsage::VertexBuffer;
        }
        if (flags & vk::BufferUsageFlagBits::eIndirectBuffer) {
            usage |= BufferUsage::IndirectBuffer;
        }

        return usage;
    }

    vk::ImageUsageFlags VulkanUtils::toVkImageUsage(TextureUsage usage)
    {
        vk::ImageUsageFlags flags{};

        if (hasFlag(usage, TextureUsage::TransferSrc)) {
            flags |= vk::ImageUsageFlagBits::eTransferSrc;
        }
        if (hasFlag(usage, TextureUsage::TransferDst)) {
            flags |= vk::ImageUsageFlagBits::eTransferDst;
        }
        if (hasFlag(usage, TextureUsage::Sampled)) {
            flags |= vk::ImageUsageFlagBits::eSampled;
        }
        if (hasFlag(usage, TextureUsage::Storage)) {
            flags |= vk::ImageUsageFlagBits::eStorage;
        }
        if (hasFlag(usage, TextureUsage::ColorAttachment)) {
            flags |= vk::ImageUsageFlagBits::eColorAttachment;
        }
        if (hasFlag(usage, TextureUsage::DepthStencilAttachment)) {
            flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        }
        if (hasFlag(usage, TextureUsage::InputAttachment)) {
            flags |= vk::ImageUsageFlagBits::eInputAttachment;
        }

        return flags;
    }

    TextureUsage VulkanUtils::fromVkImageUsage(vk::ImageUsageFlags flags)
    {
        TextureUsage usage = TextureUsage::None;

        if (flags & vk::ImageUsageFlagBits::eTransferSrc) {
            usage |= TextureUsage::TransferSrc;
        }
        if (flags & vk::ImageUsageFlagBits::eTransferDst) {
            usage |= TextureUsage::TransferDst;
        }
        if (flags & vk::ImageUsageFlagBits::eSampled) {
            usage |= TextureUsage::Sampled;
        }
        if (flags & vk::ImageUsageFlagBits::eStorage) {
            usage |= TextureUsage::Storage;
        }
        if (flags & vk::ImageUsageFlagBits::eColorAttachment) {
            usage |= TextureUsage::ColorAttachment;
        }
        if (flags & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
            usage |= TextureUsage::DepthStencilAttachment;
        }

        return usage;
    }

    VmaMemoryUsage VulkanUtils::toVmaMemoryUsage(MemoryUsage usage)
    {
        switch (usage)
        {
        case MemoryUsage::GPUOnly: return VMA_MEMORY_USAGE_GPU_ONLY;
        case MemoryUsage::CPUToGPU: return VMA_MEMORY_USAGE_CPU_TO_GPU;
        case MemoryUsage::GPUToCPU: return VMA_MEMORY_USAGE_GPU_TO_CPU;
        case MemoryUsage::CPUOnly: return VMA_MEMORY_USAGE_CPU_ONLY;
        default: return VMA_MEMORY_USAGE_AUTO;
        }
    }

    vk::ShaderStageFlags VulkanUtils::toVkShaderStage(ShaderStage stage)
    {
        vk::ShaderStageFlags flags{};

        if (hasFlag(stage, ShaderStage::Vertex)) {
            flags |= vk::ShaderStageFlagBits::eVertex;
        }
        if (hasFlag(stage, ShaderStage::Fragment)) {
            flags |= vk::ShaderStageFlagBits::eFragment;
        }
        if (hasFlag(stage, ShaderStage::Geometry)) {
            flags |= vk::ShaderStageFlagBits::eGeometry;
        }
        if (hasFlag(stage, ShaderStage::Compute)) {
            flags |= vk::ShaderStageFlagBits::eCompute;
        }
        if (hasFlag(stage, ShaderStage::TessControl)) {
            flags |= vk::ShaderStageFlagBits::eTessellationControl;
        }
        if (hasFlag(stage, ShaderStage::TessEval)) {
            flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
        }

        return flags;
    }

    ShaderStage VulkanUtils::fromVkShaderStage(vk::ShaderStageFlags flags)
    {
        uint32_t stage = 0;

        if (flags & vk::ShaderStageFlagBits::eVertex) {
            stage |= static_cast<uint32_t>(ShaderStage::Vertex);
}
        if (flags & vk::ShaderStageFlagBits::eFragment) {
            stage |= static_cast<uint32_t>(ShaderStage::Fragment);
}
        if (flags & vk::ShaderStageFlagBits::eGeometry) {
            stage |= static_cast<uint32_t>(ShaderStage::Geometry);
}
        if (flags & vk::ShaderStageFlagBits::eCompute) {
            stage |= static_cast<uint32_t>(ShaderStage::Compute);
}
        if (flags & vk::ShaderStageFlagBits::eTessellationControl) {
            stage |= static_cast<uint32_t>(ShaderStage::TessControl);
}
        if (flags & vk::ShaderStageFlagBits::eTessellationEvaluation) {
            stage |= static_cast<uint32_t>(ShaderStage::TessEval);
}

        return static_cast<ShaderStage>(stage);
    }

    vk::PipelineStageFlags2 VulkanUtils::toVkPipelineStage(ShaderStage stage)
    {
        if (stage == ShaderStage::None)
        {
            return vk::PipelineStageFlagBits2::eNone;
        }
        if (stage == ShaderStage::Host)
        {
            return vk::PipelineStageFlagBits2::eHost;
        }
        if (stage == ShaderStage::All)
        {
            return vk::PipelineStageFlagBits2::eAllCommands;
        }

        vk::PipelineStageFlags2 flags{};
        if (hasFlag(stage, ShaderStage::Vertex))
        {
            flags |= vk::PipelineStageFlagBits2::eVertexShader;
        }
        if (hasFlag(stage, ShaderStage::Fragment))
        {
            flags |= vk::PipelineStageFlagBits2::eFragmentShader;
        }
        if (hasFlag(stage, ShaderStage::Geometry))
        {
            flags |= vk::PipelineStageFlagBits2::eGeometryShader;
        }
        if (hasFlag(stage, ShaderStage::Compute))
        {
            flags |= vk::PipelineStageFlagBits2::eComputeShader;
        }

        if (hasFlag(stage, ShaderStage::TessEval))
        {
            flags |= vk::PipelineStageFlagBits2::eTessellationEvaluationShader;
        }
        if (hasFlag(stage, ShaderStage::TessControl))
        {
            flags |= vk::PipelineStageFlagBits2::eTessellationControlShader;
        }
        if (hasFlag(stage, ShaderStage::RenderTarget))
        {
            flags |= vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests;
        }
        if (hasFlag(stage, ShaderStage::Transfer))
        {
            flags |= vk::PipelineStageFlagBits2::eTransfer;
        }
        if (hasFlag(stage, ShaderStage::Host))
        {
            flags |= vk::PipelineStageFlagBits2::eHost;
        }

        return flags;
    }

    vk::ImageLayout VulkanUtils::toVkImageLayout(ResourceLayout layout)
    {
        switch (layout)
        {
        case ResourceLayout::Undefined: return vk::ImageLayout::eUndefined;
        case ResourceLayout::General: return vk::ImageLayout::eGeneral;
        case ResourceLayout::ColorAttachment: return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceLayout::DepthStencilAttachment: return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceLayout::DepthStencilReadOnly: return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ResourceLayout::ShaderReadOnly: return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceLayout::TransferSrc: return vk::ImageLayout::eTransferSrcOptimal;
        case ResourceLayout::TransferDst: return vk::ImageLayout::eTransferDstOptimal;
        case ResourceLayout::Present: return vk::ImageLayout::ePresentSrcKHR;
        default: return vk::ImageLayout::eUndefined;
        }
    }

    vk::PrimitiveTopology VulkanUtils::toVkTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::PointList: return vk::PrimitiveTopology::ePointList;
        case PrimitiveTopology::LineList: return vk::PrimitiveTopology::eLineList;
        case PrimitiveTopology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
        case PrimitiveTopology::TriangleList: return vk::PrimitiveTopology::eTriangleList;
        case PrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
        case PrimitiveTopology::TriangleFan: return vk::PrimitiveTopology::eTriangleFan;
        case PrimitiveTopology::PatchList: return vk::PrimitiveTopology::ePatchList;
        default: return vk::PrimitiveTopology::eTriangleList;
        }
    }

    PrimitiveTopology VulkanUtils::fromVkTopology(vk::PrimitiveTopology topology)
    {
        switch (topology)
        {
        case vk::PrimitiveTopology::ePointList: return PrimitiveTopology::PointList;
        case vk::PrimitiveTopology::eLineList: return PrimitiveTopology::LineList;
        case vk::PrimitiveTopology::eLineStrip: return PrimitiveTopology::LineStrip;
        case vk::PrimitiveTopology::eTriangleList: return PrimitiveTopology::TriangleList;
        case vk::PrimitiveTopology::eTriangleStrip: return PrimitiveTopology::TriangleStrip;
        case vk::PrimitiveTopology::eTriangleFan: return PrimitiveTopology::TriangleFan;
        case vk::PrimitiveTopology::ePatchList: return PrimitiveTopology::PatchList;
        default: return PrimitiveTopology::TriangleList;
        }
    }

    vk::PolygonMode VulkanUtils::toVkPolygonMode(PolygonMode mode)
    {
        switch (mode)
        {
        case PolygonMode::Fill: return vk::PolygonMode::eFill;
        case PolygonMode::Line: return vk::PolygonMode::eLine;
        case PolygonMode::Point: return vk::PolygonMode::ePoint;
        default: return vk::PolygonMode::eFill;
        }
    }

    PolygonMode VulkanUtils::fromVkPolygonMode(vk::PolygonMode mode)
    {
        switch (mode)
        {
        case vk::PolygonMode::eFill: return PolygonMode::Fill;
        case vk::PolygonMode::eLine: return PolygonMode::Line;
        case vk::PolygonMode::ePoint: return PolygonMode::Point;
        default: return PolygonMode::Fill;
        }
    }

    vk::CullModeFlags VulkanUtils::toVkCullMode(CullMode mode)
    {
        switch (mode)
        {
        case CullMode::None: return vk::CullModeFlagBits::eNone;
        case CullMode::Front: return vk::CullModeFlagBits::eFront;
        case CullMode::Back: return vk::CullModeFlagBits::eBack;
        case CullMode::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
        default: return vk::CullModeFlagBits::eNone;
        }
    }

    CullMode VulkanUtils::fromVkCullMode(vk::CullModeFlags flags)
    {
        if (flags & vk::CullModeFlagBits::eFrontAndBack) { return CullMode::FrontAndBack;
}
        if (flags & vk::CullModeFlagBits::eFront) { return CullMode::Front;
}
        if (flags & vk::CullModeFlagBits::eBack) { return CullMode::Back;
}
        return CullMode::None;
    }

    vk::CompareOp VulkanUtils::toVkCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never: return vk::CompareOp::eNever;
        case CompareOp::Less: return vk::CompareOp::eLess;
        case CompareOp::Equal: return vk::CompareOp::eEqual;
        case CompareOp::LessOrEqual: return vk::CompareOp::eLessOrEqual;
        case CompareOp::Greater: return vk::CompareOp::eGreater;
        case CompareOp::NotEqual: return vk::CompareOp::eNotEqual;
        case CompareOp::GreaterOrEqual: return vk::CompareOp::eGreaterOrEqual;
        case CompareOp::Always: return vk::CompareOp::eAlways;
        default: return vk::CompareOp::eLess;
        }
    }

    CompareOp VulkanUtils::fromVkCompareOp(vk::CompareOp op)
    {
        switch (op)
        {
        case vk::CompareOp::eNever: return CompareOp::Never;
        case vk::CompareOp::eLess: return CompareOp::Less;
        case vk::CompareOp::eEqual: return CompareOp::Equal;
        case vk::CompareOp::eLessOrEqual: return CompareOp::LessOrEqual;
        case vk::CompareOp::eGreater: return CompareOp::Greater;
        case vk::CompareOp::eNotEqual: return CompareOp::NotEqual;
        case vk::CompareOp::eGreaterOrEqual: return CompareOp::GreaterOrEqual;
        case vk::CompareOp::eAlways: return CompareOp::Always;
        default: return CompareOp::Less;
        }
    }

    vk::BlendFactor VulkanUtils::toVkBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero: return vk::BlendFactor::eZero;
        case BlendFactor::One: return vk::BlendFactor::eOne;
        case BlendFactor::SrcColor: return vk::BlendFactor::eSrcColor;
        case BlendFactor::OneMinusSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::DstColor: return vk::BlendFactor::eDstColor;
        case BlendFactor::OneMinusDstColor: return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::SrcAlpha: return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::OneMinusSrcAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::DstAlpha: return vk::BlendFactor::eDstAlpha;
        case BlendFactor::OneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
        default: return vk::BlendFactor::eOne;
        }
    }

    BlendFactor VulkanUtils::fromVkBlendFactor(vk::BlendFactor factor)
    {
        switch (factor)
        {
        case vk::BlendFactor::eZero: return BlendFactor::Zero;
        case vk::BlendFactor::eOne: return BlendFactor::One;
        case vk::BlendFactor::eSrcColor: return BlendFactor::SrcColor;
        case vk::BlendFactor::eOneMinusSrcColor: return BlendFactor::OneMinusSrcColor;
        case vk::BlendFactor::eDstColor: return BlendFactor::DstColor;
        case vk::BlendFactor::eOneMinusDstColor: return BlendFactor::OneMinusDstColor;
        case vk::BlendFactor::eSrcAlpha: return BlendFactor::SrcAlpha;
        case vk::BlendFactor::eOneMinusSrcAlpha: return BlendFactor::OneMinusSrcAlpha;
        case vk::BlendFactor::eDstAlpha: return BlendFactor::DstAlpha;
        case vk::BlendFactor::eOneMinusDstAlpha: return BlendFactor::OneMinusDstAlpha;
        default: return BlendFactor::One;
        }
    }

    vk::BlendOp VulkanUtils::toVkBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add: return vk::BlendOp::eAdd;
        case BlendOp::Subtract: return vk::BlendOp::eSubtract;
        case BlendOp::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
        case BlendOp::Min: return vk::BlendOp::eMin;
        case BlendOp::Max: return vk::BlendOp::eMax;
        default: return vk::BlendOp::eAdd;
        }
    }

    BlendOp VulkanUtils::fromVkBlendOp(vk::BlendOp op)
    {
        switch (op)
        {
        case vk::BlendOp::eAdd: return BlendOp::Add;
        case vk::BlendOp::eSubtract: return BlendOp::Subtract;
        case vk::BlendOp::eReverseSubtract: return BlendOp::ReverseSubtract;
        case vk::BlendOp::eMin: return BlendOp::Min;
        case vk::BlendOp::eMax: return BlendOp::Max;
        default: return BlendOp::Add;
        }
    }

    vk::Filter VulkanUtils::toVkFilter(Filter filter)
    {
        switch (filter)
        {
        case Filter::Nearest: return vk::Filter::eNearest;
        case Filter::Linear: return vk::Filter::eLinear;
        default: return vk::Filter::eLinear;
        }
    }

    Filter VulkanUtils::fromVkFilter(vk::Filter filter)
    {
        switch (filter)
        {
        case vk::Filter::eNearest: return Filter::Nearest;
        case vk::Filter::eLinear: return Filter::Linear;
        default: return Filter::Linear;
        }
    }

    vk::SamplerAddressMode VulkanUtils::toVkAddressMode(SamplerAddressMode mode)
    {
        switch (mode)
        {
        case SamplerAddressMode::Repeat: return vk::SamplerAddressMode::eRepeat;
        case SamplerAddressMode::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
        case SamplerAddressMode::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
        case SamplerAddressMode::ClampToBorder: return vk::SamplerAddressMode::eClampToBorder;
        default: return vk::SamplerAddressMode::eRepeat;
        }
    }

    SamplerAddressMode VulkanUtils::fromVkAddressMode(vk::SamplerAddressMode mode)
    {
        switch (mode)
        {
        case vk::SamplerAddressMode::eRepeat: return SamplerAddressMode::Repeat;
        case vk::SamplerAddressMode::eMirroredRepeat: return SamplerAddressMode::MirroredRepeat;
        case vk::SamplerAddressMode::eClampToEdge: return SamplerAddressMode::ClampToEdge;
        case vk::SamplerAddressMode::eClampToBorder: return SamplerAddressMode::ClampToBorder;
        default: return SamplerAddressMode::Repeat;
        }
    }

    vk::AttachmentLoadOp VulkanUtils::toVkLoadOp(LoadOp op)
    {
        switch (op)
        {
        case LoadOp::Load: return vk::AttachmentLoadOp::eLoad;
        case LoadOp::Clear: return vk::AttachmentLoadOp::eClear;
        case LoadOp::DontCare: return vk::AttachmentLoadOp::eDontCare;
        default: return vk::AttachmentLoadOp::eDontCare;
        }
    }

    vk::AttachmentStoreOp VulkanUtils::toVkStoreOp(StoreOp op)
    {
        switch (op)
        {
        case StoreOp::Store: return vk::AttachmentStoreOp::eStore;
        case StoreOp::DontCare: return vk::AttachmentStoreOp::eDontCare;
        default: return vk::AttachmentStoreOp::eStore;
        }
    }

    vk::DescriptorType VulkanUtils::toVkDescriptorType(DescriptorType type)
    {
        switch (type)
        {
        case DescriptorType::Sampler: return vk::DescriptorType::eSampler;
        case DescriptorType::CombinedImageSampler: return vk::DescriptorType::eCombinedImageSampler;
        case DescriptorType::SampledImage: return vk::DescriptorType::eSampledImage;
        case DescriptorType::StorageImage: return vk::DescriptorType::eStorageImage;
        case DescriptorType::UniformBuffer: return vk::DescriptorType::eUniformBuffer;
        case DescriptorType::StorageBuffer: return vk::DescriptorType::eStorageBuffer;
        case DescriptorType::UniformBufferDynamic: return vk::DescriptorType::eUniformBufferDynamic;
        case DescriptorType::StorageBufferDynamic: return vk::DescriptorType::eStorageBufferDynamic;
        default: return vk::DescriptorType::eUniformBuffer;
        }
    }

    DescriptorType VulkanUtils::fromVkDescriptorType(vk::DescriptorType type)
    {
        switch (type)
        {
        case vk::DescriptorType::eSampler: return DescriptorType::Sampler;
        case vk::DescriptorType::eCombinedImageSampler: return DescriptorType::CombinedImageSampler;
        case vk::DescriptorType::eSampledImage: return DescriptorType::SampledImage;
        case vk::DescriptorType::eStorageImage: return DescriptorType::StorageImage;
        case vk::DescriptorType::eUniformBuffer: return DescriptorType::UniformBuffer;
        case vk::DescriptorType::eStorageBuffer: return DescriptorType::StorageBuffer;
        case vk::DescriptorType::eUniformBufferDynamic: return DescriptorType::UniformBufferDynamic;
        case vk::DescriptorType::eStorageBufferDynamic: return DescriptorType::StorageBufferDynamic;
        default: return DescriptorType::UniformBuffer;
        }
    }

    vk::Viewport VulkanUtils::toVkViewport(const Viewport& viewport)
    {
        return vk::Viewport{
            viewport.x,
            viewport.y + viewport.height,
            viewport.width,
            -viewport.height,
            viewport.minDepth,
            viewport.maxDepth
        };
    }

    vk::Rect2D VulkanUtils::toVkRect2D(const Rect2D& rect)
    {
        return vk::Rect2D{
            vk::Offset2D{rect.x, rect.y},
            vk::Extent2D{rect.width, rect.height}
        };
    }

    vk::Extent2D VulkanUtils::toVkExtent2D(const Extent2D& extent)
    {
        return vk::Extent2D{extent.width, extent.height};
    }

    vk::Extent3D VulkanUtils::toVkExtent3D(const Extent3D& extent)
    {
        return vk::Extent3D{extent.width, extent.height, extent.depth};
    }

    vk::ClearValue VulkanUtils::toVkClearValue(const ClearValue& clearValue)
    {
        vk::ClearValue vkClear;

        if (clearValue.isDepthStencil)
        {
            vkClear.depthStencil.depth = clearValue.depthStencil.depth;
            vkClear.depthStencil.stencil = clearValue.depthStencil.stencil;
        }
        else
        {
            std::memcpy(vkClear.color.float32, clearValue.color.float32, sizeof(float) * 4);
        }

        return vkClear;
    }

    vk::DynamicState VulkanUtils::toVkDynamicState(DynamicState state)
    {
        switch (state)
        {
        case DynamicState::Viewport: return vk::DynamicState::eViewport;
        case DynamicState::Scissor: return vk::DynamicState::eScissor;
        case DynamicState::LineWidth: return vk::DynamicState::eLineWidth;
        case DynamicState::DepthBias: return vk::DynamicState::eDepthBias;
        case DynamicState::BlendConstants: return vk::DynamicState::eBlendConstants;
        case DynamicState::DepthBounds: return vk::DynamicState::eDepthBounds;
        case DynamicState::StencilCompareMask: return vk::DynamicState::eStencilCompareMask;
        case DynamicState::StencilWriteMask: return vk::DynamicState::eStencilWriteMask;
        case DynamicState::StencilReference: return vk::DynamicState::eStencilReference;
        case DynamicState::CullMode: return vk::DynamicState::eCullMode;
        case DynamicState::FrontFace: return vk::DynamicState::eFrontFace;
        case DynamicState::PrimitiveTopology: return vk::DynamicState::ePrimitiveTopology;
        case DynamicState::ViewportWithCount: return vk::DynamicState::eViewportWithCount;
        case DynamicState::ScissorWithCount: return vk::DynamicState::eScissorWithCount;
        case DynamicState::VertexInputBindingStride: return vk::DynamicState::eVertexInputBindingStride;
        case DynamicState::DepthTestEnable: return vk::DynamicState::eDepthTestEnable;
        case DynamicState::DepthWriteEnable: return vk::DynamicState::eDepthWriteEnable;
        case DynamicState::DepthCompareOp: return vk::DynamicState::eDepthCompareOp;
        case DynamicState::DepthBoundsTestEnable: return vk::DynamicState::eDepthBoundsTestEnable;
        case DynamicState::StencilTestEnable: return vk::DynamicState::eStencilTestEnable;
        case DynamicState::StencilOp: return vk::DynamicState::eStencilOp;
        case DynamicState::RasterizerDiscardEnable: return vk::DynamicState::eRasterizerDiscardEnable;
        case DynamicState::DepthBiasEnable: return vk::DynamicState::eDepthBiasEnable;
        case DynamicState::PrimitiveRestartEnable: return vk::DynamicState::ePrimitiveRestartEnable;
        default: return vk::DynamicState::eViewport;
        }
    }

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> VulkanUtils::getLayoutStageAccess(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            return {vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlags2{}};

        case vk::ImageLayout::eTransferDstOptimal:
            return {vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};

        case vk::ImageLayout::eTransferSrcOptimal:
            return {vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};

        case vk::ImageLayout::eColorAttachmentOptimal:
            return {vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite};

        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return {vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead};

        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return {vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader,
                    vk::AccessFlagBits2::eShaderRead};

        case vk::ImageLayout::eGeneral:
            return {vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};

        case vk::ImageLayout::ePresentSrcKHR:
            return {vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlags2{}};

        default:
            // Fallback for unhandled layouts
            return {vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
        }
    }
} // namespace pnkr::renderer::rhi::vulkan
