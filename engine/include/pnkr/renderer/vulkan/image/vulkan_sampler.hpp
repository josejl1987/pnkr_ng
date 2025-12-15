

#ifndef PNKR_VULKAN_SAMPLER_HPP
#define PNKR_VULKAN_SAMPLER_HPP
#pragma once

#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

    class VulkanSampler {
    public:
        explicit VulkanSampler(vk::Device device,
                              vk::Filter magFilter = vk::Filter::eLinear,
                              vk::Filter minFilter = vk::Filter::eLinear,
                              vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat);

        ~VulkanSampler();

        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;

        VulkanSampler(VulkanSampler&& other) noexcept;
        VulkanSampler& operator=(VulkanSampler&& other) noexcept;

        [[nodiscard]] vk::Sampler sampler() const noexcept { return m_sampler; }

    private:
        void destroy() noexcept;

        vk::Device m_device{};
        vk::Sampler m_sampler{};
    };

}

#endif