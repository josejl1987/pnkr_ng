#pragma once

#include "pnkr/rhi/rhi_command_buffer.hpp"
#include <vulkan/vulkan.hpp>
#include <span>
#include <vector>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    namespace VulkanBarrierBuilder
    {
        void buildBarriers(
            const VulkanRHIDevice& device,
            uint32_t queueFamilyIndex,
            ShaderStageFlags srcStage,
            ShaderStageFlags dstStage,
            std::span<const RHIMemoryBarrier> barriers,
            std::vector<vk::BufferMemoryBarrier2>& outBufferBarriers,
            std::vector<vk::ImageMemoryBarrier2>& outImageBarriers
        );
    }
}
