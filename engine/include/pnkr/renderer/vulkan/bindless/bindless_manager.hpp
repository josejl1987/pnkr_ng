

#pragma once

#include <vulkan/vulkan.hpp>

namespace pnkr::renderer {

class VulkanBuffer;
class VulkanImage;

struct BindlessIndex {
    uint32_t index = UINT32_MAX;

    [[nodiscard]] bool isValid() const { return index != UINT32_MAX; }

    [[nodiscard]] uint32_t raw() const { return index; }
};

constexpr BindlessIndex INVALID_BINDLESS_INDEX{UINT32_MAX};

class BindlessManager {
public:
    explicit BindlessManager(vk::Device device, vk::PhysicalDevice physicalDevice);
    ~BindlessManager();
    void createDescriptorPool();

    BindlessManager(const BindlessManager&) = delete;
    BindlessManager& operator=(const BindlessManager&) = delete;

    [[nodiscard]] BindlessIndex registerStorageBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
    [[nodiscard]] BindlessIndex registerSampledImage(vk::ImageView view, vk::Sampler sampler);
    [[nodiscard]] BindlessIndex registerStorageImage(vk::ImageView view);
    [[nodiscard]] BindlessIndex registerCubemap(vk::ImageView view, vk::Sampler sampler);
    void logStats() const;

    [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return m_descriptorSet; }
    [[nodiscard]] vk::DescriptorSetLayout getLayout() const { return m_layout; }

    [[nodiscard]] uint32_t maxStorageBuffers() const { return m_maxStorageBuffers; }
    [[nodiscard]] uint32_t maxSampledImages() const { return m_maxSampledImages; }
    [[nodiscard]] uint32_t maxStorageImages() const { return m_maxStorageImages; }
    [[nodiscard]] uint32_t maxCubemaps() const { return m_maxCubemaps; }

    [[nodiscard]] uint32_t currentStorageBufferCount() const { return m_storageBufferCount; }
    [[nodiscard]] uint32_t currentSampledImageCount() const { return m_sampledImageCount; }
    [[nodiscard]] uint32_t currentStorageImageCount() const { return m_storageImageCount; }
    [[nodiscard]] uint32_t currentCubemapCount() const { return m_cubemapCount; }

private:
    vk::Device m_device;
    vk::DescriptorPool m_pool;
    vk::DescriptorSet m_descriptorSet;
    vk::DescriptorSetLayout m_layout;

    static constexpr uint32_t MAX_STORAGE_BUFFERS = 100'000;
    static constexpr uint32_t MAX_SAMPLED_IMAGES = 100'000;  // Can be huge
    static constexpr uint32_t MAX_STORAGE_IMAGES = 10'000;
    static constexpr uint32_t MAX_CUBEMAPS = 1000;

    uint32_t m_maxStorageBuffers = MAX_STORAGE_BUFFERS;
    uint32_t m_maxSampledImages = MAX_SAMPLED_IMAGES;
    uint32_t m_maxStorageImages = MAX_STORAGE_IMAGES;
    uint32_t m_maxCubemaps = MAX_CUBEMAPS;

    uint32_t m_storageBufferCount = 0;
    uint32_t m_sampledImageCount = 0;
    uint32_t m_storageImageCount = 0;
    uint32_t m_cubemapCount = 0;

    void createDescriptorLayout();
    void createDescriptorSet();
};

}
