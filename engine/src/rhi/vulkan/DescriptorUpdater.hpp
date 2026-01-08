#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <deque>

namespace pnkr::renderer::rhi::vulkan
{
    class VulkanRHIDevice;

    class DescriptorUpdater {
    public:
        explicit DescriptorUpdater(vk::Device device, vk::DescriptorSet set)
            : m_device(device), m_set(set) {}

        DescriptorUpdater& writeBuffer(uint32_t binding, vk::DescriptorType type, vk::Buffer buffer, uint64_t offset, uint64_t range) {
            auto& info = m_bufferInfos.emplace_back();
            info.buffer = buffer;
            info.offset = offset;
            info.range = range;

            auto& write = m_writes.emplace_back();
            write.dstSet = m_set;
            write.dstBinding = binding;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pBufferInfo = &info;
            return *this;
        }

        DescriptorUpdater& writeImage(uint32_t binding, vk::DescriptorType type, vk::ImageView view, vk::ImageLayout layout, vk::Sampler sampler = nullptr) {
            auto& info = m_imageInfos.emplace_back();
            info.imageView = view;
            info.imageLayout = layout;
            info.sampler = sampler;

            auto& write = m_writes.emplace_back();
            write.dstSet = m_set;
            write.dstBinding = binding;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pImageInfo = &info;
            return *this;
        }

        void commit();

    private:
        vk::Device m_device;
        vk::DescriptorSet m_set;
        std::vector<vk::WriteDescriptorSet> m_writes;
        std::deque<vk::DescriptorBufferInfo> m_bufferInfos;
        std::deque<vk::DescriptorImageInfo> m_imageInfos;
    };
}
