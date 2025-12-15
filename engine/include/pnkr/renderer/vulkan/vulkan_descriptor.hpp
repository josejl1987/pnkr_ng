#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>

namespace pnkr::renderer {

class VulkanDescriptorAllocator {
public:
  explicit VulkanDescriptorAllocator(vk::Device device);
  ~VulkanDescriptorAllocator();

  VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
  VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;

  vk::DescriptorSet allocate(vk::DescriptorSetLayout layout);
  void reset();
  [[nodiscard]] vk::Device device() const noexcept { return m_device; }
private:
  vk::DescriptorPool createPool();

  vk::Device m_device{};
  vk::DescriptorPool m_currentPool{};
  std::vector<vk::DescriptorPool> m_usedPools;
  std::vector<vk::DescriptorPool> m_freePools;
};

class VulkanDescriptorLayoutCache {
public:
  explicit VulkanDescriptorLayoutCache(vk::Device device);
  ~VulkanDescriptorLayoutCache();

  VulkanDescriptorLayoutCache(const VulkanDescriptorLayoutCache&) = delete;
  VulkanDescriptorLayoutCache& operator=(const VulkanDescriptorLayoutCache&) = delete;

  vk::DescriptorSetLayout createLayout(const vk::DescriptorSetLayoutCreateInfo& info);
  void cleanup();

private:
  struct DescriptorLayoutInfo {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    bool operator==(const DescriptorLayoutInfo& other) const;
    size_t hash() const;
  };

  struct DescriptorLayoutHash {
    std::size_t operator()(const DescriptorLayoutInfo& info) const {
      return info.hash();
    }
  };

  vk::Device m_device{};
  std::unordered_map<DescriptorLayoutInfo, vk::DescriptorSetLayout, DescriptorLayoutHash> m_layoutCache;
};

class VulkanDescriptorBuilder {
  friend class VulkanDescriptorAllocator;
public:
  static VulkanDescriptorBuilder begin(VulkanDescriptorLayoutCache* cache,
                                       VulkanDescriptorAllocator* allocator);

  VulkanDescriptorBuilder& bindImage(uint32_t binding,
                                     vk::DescriptorImageInfo* imageInfo,
                                     vk::DescriptorType type,
                                     vk::ShaderStageFlags stageFlags);

  VulkanDescriptorBuilder& bindBuffer(uint32_t binding,
                                      vk::DescriptorBufferInfo* bufferInfo,
                                      vk::DescriptorType type,
                                      vk::ShaderStageFlags stageFlags);

  bool build(vk::DescriptorSet& set, vk::DescriptorSetLayout& layout);
  bool build(vk::DescriptorSet& set);

private:
  std::vector<vk::WriteDescriptorSet> m_writes;
  std::vector<vk::DescriptorSetLayoutBinding> m_bindings;

  VulkanDescriptorLayoutCache* m_cache{nullptr};
  VulkanDescriptorAllocator* m_allocator{nullptr};
};

}
