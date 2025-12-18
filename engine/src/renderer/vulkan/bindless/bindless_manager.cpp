#include "pnkr/renderer/vulkan/bindless/bindless_manager.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer {

BindlessManager::BindlessManager(vk::Device device, vk::PhysicalDevice physicalDevice)
    : m_device(device)
{
    // Query device limits for descriptor counts
    vk::PhysicalDeviceDescriptorIndexingPropertiesEXT indexingProps;
    vk::PhysicalDeviceProperties2 props;
    props.pNext = &indexingProps;
    physicalDevice.getProperties2(&props);

    // Adjust limits based on device capabilities
    m_maxStorageBuffers =
        std::min(m_maxStorageBuffers,
                 indexingProps.maxDescriptorSetUpdateAfterBindStorageBuffers);
    m_maxSampledImages =
        std::min(m_maxSampledImages,
                 indexingProps.maxDescriptorSetUpdateAfterBindSampledImages);
    m_maxStorageImages =
        std::min(m_maxStorageImages,
                 indexingProps.maxDescriptorSetUpdateAfterBindStorageImages);
    m_maxCubemaps =
        std::min(m_maxCubemaps,
                 indexingProps.maxDescriptorSetUpdateAfterBindSampledImages);

    core::Logger::info("BindlessManager created with limits: "
              "StorageBuffers={}, SampledImages={}, StorageImages={}, Cubemaps={}",
              m_maxStorageBuffers, m_maxSampledImages, m_maxStorageImages, m_maxCubemaps);

    createDescriptorPool();
    createDescriptorLayout();
}

BindlessManager::~BindlessManager()
{
    if (m_pool) {
        m_device.destroyDescriptorPool(m_pool);
    }
    if (m_layout) {
        m_device.destroyDescriptorSetLayout(m_layout);
    }
}

void BindlessManager::createDescriptorPool()
{
    vk::DescriptorPoolSize poolSizes[4];
    poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[0].descriptorCount = m_maxStorageBuffers;

    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = m_maxSampledImages;

    poolSizes[2].type = vk::DescriptorType::eStorageImage;
    poolSizes[2].descriptorCount = m_maxStorageImages;

    poolSizes[3].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[3].descriptorCount = m_maxCubemaps;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;

    m_pool = m_device.createDescriptorPool(poolInfo);
}

void BindlessManager::createDescriptorLayout()
{
    // Array of bindings
    vk::DescriptorSetLayoutBinding bindings[4];

    // Storage buffers
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[0].descriptorCount = m_maxStorageBuffers;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eAll;
    bindings[0].pImmutableSamplers = nullptr;

    // Sampled images
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].descriptorCount = m_maxSampledImages;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eAll;
    bindings[1].pImmutableSamplers = nullptr;

    // Storage images
    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[2].descriptorCount = m_maxStorageImages;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eAll;
    bindings[2].pImmutableSamplers = nullptr;

    // Cubemaps (Binding 3)
    bindings[3].binding = 3;
    bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[3].descriptorCount = m_maxCubemaps;
    bindings[3].stageFlags = vk::ShaderStageFlagBits::eAll;
    bindings[3].pImmutableSamplers = nullptr;

    // Enable update-after-bind for dynamic updates
    vk::DescriptorBindingFlags flags =
        vk::DescriptorBindingFlagBits::eUpdateAfterBind |
        vk::DescriptorBindingFlagBits::ePartiallyBound;

    vk::DescriptorBindingFlags bindingFlagsList[4] = {flags, flags, flags, flags};

    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
    bindingFlags.bindingCount = 4;
    bindingFlags.pBindingFlags = bindingFlagsList;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    layoutInfo.pNext = &bindingFlags;
    layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

    m_layout = m_device.createDescriptorSetLayout(layoutInfo);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    std::vector<vk::DescriptorSet> sets = m_device.allocateDescriptorSets(allocInfo);
    m_descriptorSet = sets[0];
}

BindlessIndex BindlessManager::registerStorageBuffer(
    vk::Buffer buffer,
    vk::DeviceSize offset,
    vk::DeviceSize range)
{
    if (m_storageBufferCount >= m_maxStorageBuffers) {
        core::Logger::error("BindlessManager: Storage buffer limit exceeded!");
        return INVALID_BINDLESS_INDEX;
    }

    uint32_t index = m_storageBufferCount++;

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    m_device.updateDescriptorSets(write, nullptr);

    return BindlessIndex{index};
}

BindlessIndex BindlessManager::registerSampledImage(vk::ImageView view, vk::Sampler sampler)
{
    if (m_sampledImageCount >= m_maxSampledImages) {
        core::Logger::error("BindlessManager: Sampled image limit exceeded!");
        return INVALID_BINDLESS_INDEX;
    }

    uint32_t index = m_sampledImageCount++;

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptorSet;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    m_device.updateDescriptorSets(write, nullptr);

    return BindlessIndex{index};
}

BindlessIndex BindlessManager::registerStorageImage(vk::ImageView view)
{
    if (m_storageImageCount >= m_maxStorageImages) {
        core::Logger::error("BindlessManager: Storage image limit exceeded!");
        return INVALID_BINDLESS_INDEX;
    }

    uint32_t index = m_storageImageCount++;

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;
    imageInfo.imageView = view;
    imageInfo.sampler = nullptr;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptorSet;
    write.dstBinding = 2;
    write.dstArrayElement = index;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    m_device.updateDescriptorSets(write, nullptr);

    return BindlessIndex{index};
}

BindlessIndex BindlessManager::registerCubemap(vk::ImageView view, vk::Sampler sampler)
{
    if (m_cubemapCount >= m_maxCubemaps) {
        core::Logger::error("BindlessManager: Cubemap limit exceeded!");
        return INVALID_BINDLESS_INDEX;
    }

    uint32_t index = m_cubemapCount++;

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet write{};
    write.dstSet = m_descriptorSet;
    write.dstBinding = 3;  // Cubemap binding
    write.dstArrayElement = index;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    m_device.updateDescriptorSets(write, nullptr);

    return BindlessIndex{index};
}

void BindlessManager::logStats() const
{
    core::Logger::info("BindlessManager Stats:");
    core::Logger::info("  Storage Buffers: {}/{}", m_storageBufferCount, m_maxStorageBuffers);
    core::Logger::info("  Sampled Images:  {}/{}", m_sampledImageCount, m_maxSampledImages);
    core::Logger::info("  Storage Images:  {}/{}", m_storageImageCount, m_maxStorageImages);
    core::Logger::info("  Cubemaps:         {}/{}", m_cubemapCount, m_maxCubemaps);
}

} // namespace pnkr::renderer
