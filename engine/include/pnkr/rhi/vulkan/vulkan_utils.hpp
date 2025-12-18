#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include <vulkan/vulkan.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanUtils
    {
    public:
        // Format conversions
        static vk::Format toVkFormat(Format format);
        static Format fromVkFormat(vk::Format format);

        // Buffer usage conversions
        static vk::BufferUsageFlags toVkBufferUsage(BufferUsage usage);
        static BufferUsage fromVkBufferUsage(vk::BufferUsageFlags flags);

        // Texture usage conversions
        static vk::ImageUsageFlags toVkImageUsage(TextureUsage usage);
        static TextureUsage fromVkImageUsage(vk::ImageUsageFlags flags);

        // Memory usage to VMA flags
        static VmaMemoryUsage toVmaMemoryUsage(MemoryUsage usage);

        // Shader stage conversions
        static vk::ShaderStageFlags toVkShaderStage(ShaderStage stage);
        static ShaderStage fromVkShaderStage(vk::ShaderStageFlags flags);
        static vk::PipelineStageFlags2 toVkPipelineStage(ShaderStage stage);

        static vk::ImageLayout toVkImageLayout(ResourceLayout layout);

        // Topology conversions
        static vk::PrimitiveTopology toVkTopology(PrimitiveTopology topology);
        static PrimitiveTopology fromVkTopology(vk::PrimitiveTopology topology);

        // Polygon mode conversions
        static vk::PolygonMode toVkPolygonMode(PolygonMode mode);
        static PolygonMode fromVkPolygonMode(vk::PolygonMode mode);

        // Cull mode conversions
        static vk::CullModeFlags toVkCullMode(CullMode mode);
        static CullMode fromVkCullMode(vk::CullModeFlags flags);

        // Compare op conversions
        static vk::CompareOp toVkCompareOp(CompareOp op);
        static CompareOp fromVkCompareOp(vk::CompareOp op);

        // Blend factor conversions
        static vk::BlendFactor toVkBlendFactor(BlendFactor factor);
        static BlendFactor fromVkBlendFactor(vk::BlendFactor factor);

        // Blend op conversions
        static vk::BlendOp toVkBlendOp(BlendOp op);
        static BlendOp fromVkBlendOp(vk::BlendOp op);

        // Filter conversions
        static vk::Filter toVkFilter(Filter filter);
        static Filter fromVkFilter(vk::Filter filter);

        // Sampler address mode conversions
        static vk::SamplerAddressMode toVkAddressMode(SamplerAddressMode mode);
        static SamplerAddressMode fromVkAddressMode(vk::SamplerAddressMode mode);

        // Load/store op conversions
        static vk::AttachmentLoadOp toVkLoadOp(LoadOp op);
        static vk::AttachmentStoreOp toVkStoreOp(StoreOp op);

        // Descriptor type conversions
        static vk::DescriptorType toVkDescriptorType(DescriptorType type);
        static DescriptorType fromVkDescriptorType(vk::DescriptorType type);

        // Viewport/rect conversions
        static vk::Viewport toVkViewport(const Viewport& viewport);
        static vk::Rect2D toVkRect2D(const Rect2D& rect);
        static vk::Extent2D toVkExtent2D(const Extent2D& extent);
        static vk::Extent3D toVkExtent3D(const Extent3D& extent);

        // Clear value conversions
        static vk::ClearValue toVkClearValue(const ClearValue& clearValue);
    };

} // namespace pnkr::renderer::rhi::vulkan
