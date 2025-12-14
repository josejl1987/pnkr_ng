//
// Created by Jose on 12/14/2025.
//

#include "pnkr/renderer/vulkan/vulkan_descriptor.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer {

// ============================================================================
// VulkanDescriptorAllocator
// ============================================================================

VulkanDescriptorAllocator::VulkanDescriptorAllocator(vk::Device device)
  : m_device(device)
{
}

VulkanDescriptorAllocator::~VulkanDescriptorAllocator() {
  if (m_currentPool) {
    m_device.destroyDescriptorPool(m_currentPool);
    m_currentPool = nullptr;
  }

  for (auto pool : m_freePools)  m_device.destroyDescriptorPool(pool);
  for (auto pool : m_usedPools)  m_device.destroyDescriptorPool(pool);

  m_freePools.clear();
  m_usedPools.clear();
}

vk::DescriptorPool VulkanDescriptorAllocator::createPool() {
  std::vector<vk::DescriptorPoolSize> poolSizes = {
    {vk::DescriptorType::eSampler, 1000},
    {vk::DescriptorType::eCombinedImageSampler, 1000},
    {vk::DescriptorType::eSampledImage, 1000},
    {vk::DescriptorType::eStorageImage, 1000},
    {vk::DescriptorType::eUniformTexelBuffer, 1000},
    {vk::DescriptorType::eStorageTexelBuffer, 1000},
    {vk::DescriptorType::eUniformBuffer, 1000},
    {vk::DescriptorType::eStorageBuffer, 1000},
    {vk::DescriptorType::eUniformBufferDynamic, 1000},
    {vk::DescriptorType::eStorageBufferDynamic, 1000},
    {vk::DescriptorType::eInputAttachment, 1000}
  };

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.flags = vk::DescriptorPoolCreateFlags{};
  poolInfo.maxSets = 1000;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  return m_device.createDescriptorPool(poolInfo);
}

vk::DescriptorSet VulkanDescriptorAllocator::allocate(vk::DescriptorSetLayout layout) {
  if (!m_currentPool) {
    m_currentPool = createPool();
  }

  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_currentPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  vk::DescriptorSet set;
  auto result = m_device.allocateDescriptorSets(&allocInfo, &set);

  if (result == vk::Result::eErrorOutOfPoolMemory ||
      result == vk::Result::eErrorFragmentedPool) {
    // Current pool is full, grab a new one
    m_usedPools.push_back(m_currentPool);

    if (!m_freePools.empty()) {
      m_currentPool = m_freePools.back();
      m_freePools.pop_back();
    } else {
      m_currentPool = createPool();
    }

    // Retry allocation
    allocInfo.descriptorPool = m_currentPool;
    set = m_device.allocateDescriptorSets(allocInfo)[0];
  } else if (result != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to allocate descriptor set");
  }

  return set;
}

void VulkanDescriptorAllocator::reset() {
  for (auto pool : m_usedPools) {
    m_device.resetDescriptorPool(pool);
    m_freePools.push_back(pool);
  }
  m_usedPools.clear();

  if (m_currentPool) {
    m_device.resetDescriptorPool(m_currentPool);
    m_freePools.push_back(m_currentPool);
    m_currentPool = nullptr;
  }
}

// ============================================================================
// VulkanDescriptorLayoutCache
// ============================================================================

VulkanDescriptorLayoutCache::VulkanDescriptorLayoutCache(vk::Device device)
  : m_device(device)
{
}

VulkanDescriptorLayoutCache::~VulkanDescriptorLayoutCache() {
  cleanup();
}

vk::DescriptorSetLayout VulkanDescriptorLayoutCache::createLayout(
  const vk::DescriptorSetLayoutCreateInfo& info)
{
  DescriptorLayoutInfo layoutInfo;
  layoutInfo.bindings.reserve(info.bindingCount);

  bool isSorted = true;
  int32_t lastBinding = -1;

  for (uint32_t i = 0; i < info.bindingCount; i++) {
    layoutInfo.bindings.push_back(info.pBindings[i]);

    if (static_cast<int32_t>(info.pBindings[i].binding) > lastBinding) {
      lastBinding = info.pBindings[i].binding;
    } else {
      isSorted = false;
    }
  }

  if (!isSorted) {
    std::sort(layoutInfo.bindings.begin(), layoutInfo.bindings.end(),
              [](const vk::DescriptorSetLayoutBinding& a,
                 const vk::DescriptorSetLayoutBinding& b) {
                return a.binding < b.binding;
              });
  }

  auto it = m_layoutCache.find(layoutInfo);
  if (it != m_layoutCache.end()) {
    return it->second;
  }

  vk::DescriptorSetLayout layout = m_device.createDescriptorSetLayout(info);
  m_layoutCache[layoutInfo] = layout;
  return layout;
}

void VulkanDescriptorLayoutCache::cleanup() {
  for (auto& [info, layout] : m_layoutCache) {
    m_device.destroyDescriptorSetLayout(layout);
  }
  m_layoutCache.clear();
}

bool VulkanDescriptorLayoutCache::DescriptorLayoutInfo::operator==(
  const DescriptorLayoutInfo& other) const
{
  if (bindings.size() != other.bindings.size()) {
    return false;
  }

  for (size_t i = 0; i < bindings.size(); i++) {
    if (bindings[i].binding != other.bindings[i].binding ||
        bindings[i].descriptorType != other.bindings[i].descriptorType ||
        bindings[i].descriptorCount != other.bindings[i].descriptorCount ||
        bindings[i].stageFlags != other.bindings[i].stageFlags) {
      return false;
    }
  }

  return true;
}

size_t VulkanDescriptorLayoutCache::DescriptorLayoutInfo::hash() const {
  size_t result = std::hash<size_t>()(bindings.size());

  for (const auto& b : bindings) {
    size_t bindingHash = b.binding |
                        static_cast<uint32_t>(b.descriptorType) << 8 |
                        static_cast<uint32_t>(b.stageFlags) << 16;

    result ^= std::hash<size_t>()(bindingHash);
  }

  return result;
}

// ============================================================================
// VulkanDescriptorBuilder
// ============================================================================

VulkanDescriptorBuilder VulkanDescriptorBuilder::begin(
  VulkanDescriptorLayoutCache* cache,
  VulkanDescriptorAllocator* allocator)
{
  VulkanDescriptorBuilder builder;
  builder.m_cache = cache;
  builder.m_allocator = allocator;
  return builder;
}

VulkanDescriptorBuilder& VulkanDescriptorBuilder::bindImage(
  uint32_t binding,
  vk::DescriptorImageInfo* imageInfo,
  vk::DescriptorType type,
  vk::ShaderStageFlags stageFlags)
{
  vk::DescriptorSetLayoutBinding layoutBinding{};
  layoutBinding.binding = binding;
  layoutBinding.descriptorType = type;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = stageFlags;

  m_bindings.push_back(layoutBinding);

  vk::WriteDescriptorSet write{};
  write.dstBinding = binding;
  write.descriptorType = type;
  write.descriptorCount = 1;
  write.pImageInfo = imageInfo;

  m_writes.push_back(write);
  return *this;
}

VulkanDescriptorBuilder& VulkanDescriptorBuilder::bindBuffer(
  uint32_t binding,
  vk::DescriptorBufferInfo* bufferInfo,
  vk::DescriptorType type,
  vk::ShaderStageFlags stageFlags)
{
  vk::DescriptorSetLayoutBinding layoutBinding{};
  layoutBinding.binding = binding;
  layoutBinding.descriptorType = type;
  layoutBinding.descriptorCount = 1;
  layoutBinding.stageFlags = stageFlags;

  m_bindings.push_back(layoutBinding);

  vk::WriteDescriptorSet write{};
  write.dstBinding = binding;
  write.descriptorType = type;
  write.descriptorCount = 1;
  write.pBufferInfo = bufferInfo;

  m_writes.push_back(write);
  return *this;
}

bool VulkanDescriptorBuilder::build(vk::DescriptorSet& set,
                                    vk::DescriptorSetLayout& layout)
{
  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());
  layoutInfo.pBindings = m_bindings.data();

  layout = m_cache->createLayout(layoutInfo);

  set = m_allocator->allocate(layout);

  for (auto& write : m_writes) {
    write.dstSet = set;
  }
  vk::Device device = m_allocator->device();
  device.updateDescriptorSets(m_writes, nullptr);
  return true;
}

bool VulkanDescriptorBuilder::build(vk::DescriptorSet& set) {
  vk::DescriptorSetLayout layout;
  return build(set, layout);
}

} // namespace pnkr::renderer
