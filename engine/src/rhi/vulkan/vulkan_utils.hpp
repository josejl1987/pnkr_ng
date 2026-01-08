#pragma once

#include <vk_mem_alloc.h>

#include "pnkr/rhi/rhi_types.hpp"
#include <vulkan/vulkan.hpp>
#include <string_view>
#include "pnkr/core/logger.hpp"
#include <cpptrace/cpptrace.hpp>

namespace pnkr::renderer::rhi::vulkan
{
    template<typename CreateInfo>
    struct VkBuilder {
        CreateInfo info{};

        template<typename T, typename U>
        auto& set(T CreateInfo::* field, U&& value) {
            info.*field = std::forward<U>(value);
            return *this;
        }

        const CreateInfo& build() const { return info; }
        operator const CreateInfo&() const { return info; }
    };

    class VulkanUtils
    {
    public:

        [[nodiscard]] static bool checkVkResult(vk::Result result, std::string_view operation) {
            if (result != vk::Result::eSuccess) {
                core::Logger::RHI.error("Failed to {}: {}", operation, vk::to_string(result));
                throw cpptrace::runtime_error(std::string(operation) + " failed");
            }
            return true;
        }

        static vk::Format toVkFormat(Format format);
        static Format fromVkFormat(vk::Format format);

        static vk::BufferUsageFlags toVkBufferUsage(BufferUsageFlags usage);
        static BufferUsageFlags fromVkBufferUsage(vk::BufferUsageFlags flags);

        static vk::ImageUsageFlags toVkImageUsage(TextureUsageFlags usage);
        static TextureUsageFlags fromVkImageUsage(vk::ImageUsageFlags flags);

        static vk::SampleCountFlagBits toVkSampleCount(uint32_t count);

        static VmaMemoryUsage toVmaMemoryUsage(MemoryUsage usage);

        static vk::ShaderStageFlags toVkShaderStage(ShaderStageFlags stage);
        static ShaderStageFlags fromVkShaderStage(vk::ShaderStageFlags flags);
        static vk::PipelineStageFlags2 toVkPipelineStage(ShaderStageFlags stage);

        static vk::ImageLayout toVkImageLayout(ResourceLayout layout);

        static vk::PrimitiveTopology toVkTopology(PrimitiveTopology topology);
        static PrimitiveTopology fromVkTopology(vk::PrimitiveTopology topology);

        static vk::PolygonMode toVkPolygonMode(PolygonMode mode);
        static PolygonMode fromVkPolygonMode(vk::PolygonMode mode);

        static vk::CullModeFlags toVkCullMode(CullMode mode);
        static CullMode fromVkCullMode(vk::CullModeFlags flags);

        static vk::CompareOp toVkCompareOp(CompareOp op);
        static CompareOp fromVkCompareOp(vk::CompareOp op);

        static vk::BlendFactor toVkBlendFactor(BlendFactor factor);
        static BlendFactor fromVkBlendFactor(vk::BlendFactor factor);

        static vk::BlendOp toVkBlendOp(BlendOp op);
        static BlendOp fromVkBlendOp(vk::BlendOp op);

        static vk::Filter toVkFilter(Filter filter);
        static Filter fromVkFilter(vk::Filter filter);

        static vk::SamplerAddressMode toVkAddressMode(SamplerAddressMode mode);
        static SamplerAddressMode fromVkAddressMode(vk::SamplerAddressMode mode);

        static vk::AttachmentLoadOp toVkLoadOp(LoadOp op);
        static vk::AttachmentStoreOp toVkStoreOp(StoreOp op);

        static vk::DescriptorType toVkDescriptorType(DescriptorType type);
        static DescriptorType fromVkDescriptorType(vk::DescriptorType type);

        static vk::Viewport toVkViewport(const Viewport& viewport);
        static vk::Rect2D toVkRect2D(const Rect2D& rect);
        static vk::Extent2D toVkExtent2D(const Extent2D& extent);
        static vk::Extent3D toVkExtent3D(const Extent3D& extent);

        static vk::ClearValue toVkClearValue(const ClearValue& clearValue);

            static vk::DynamicState toVkDynamicState(DynamicState state);
        
            static vk::ImageAspectFlags getImageAspectMask(vk::Format format);
            static vk::ImageAspectFlags rhiToVkTextureAspectFlags(Format format);
        
            static void setDebugName(vk::Device device, vk::ObjectType type, uint64_t handle, const std::string& name);
        static std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> getLayoutStageAccess(vk::ImageLayout layout);
    };

}
