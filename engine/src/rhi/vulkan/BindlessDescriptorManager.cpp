#include "rhi/vulkan/BindlessDescriptorManager.hpp"

#include "pnkr/core/logger.hpp"
#include "rhi/vulkan/vulkan_buffer.hpp"
#include "rhi/vulkan/vulkan_descriptor.hpp"
#include "rhi/vulkan/vulkan_device.hpp"
#include "rhi/vulkan/vulkan_sampler.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "vulkan_cast.hpp"

namespace pnkr::renderer::rhi::vulkan {
BindlessDescriptorManager::BindlessDescriptorManager(
    vk::Device device, vk::PhysicalDevice physicalDevice)
    : m_device(device), m_physicalDevice(physicalDevice) {}

BindlessDescriptorManager::~BindlessDescriptorManager() {
  if (m_bindlessPool) {
    m_device.destroyDescriptorPool(m_bindlessPool);
  }
}

void BindlessDescriptorManager::init(VulkanRHIDevice *rhiDevice) {
  m_rhiDevice = rhiDevice;

  {
    TextureDescriptor desc{};
    desc.extent = {.width = 1, .height = 1, .depth = 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    desc.debugName = "BindlessDummyTexture";
    desc.skipBindless = true;
    m_dummyTexture = m_rhiDevice->createTexture(desc.debugName.c_str(), desc);

    uint32_t white = 0xFFFFFFFF;
    m_dummyTexture->uploadData(std::as_bytes(std::span(&white, 1)));
  }
  {
    TextureDescriptor desc{};
    desc.extent = {.width = 1, .height = 1, .depth = 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    desc.type = TextureType::TextureCube;
    desc.arrayLayers = 6;
    desc.debugName = "BindlessDummyCube";
    desc.skipBindless = true;
    m_dummyCube = m_rhiDevice->createTexture(desc.debugName.c_str(), desc);
  }
  {
    TextureDescriptor desc{};
    desc.extent = {.width = 1, .height = 1, .depth = 1};
    desc.format = Format::R8G8B8A8_UNORM;
    desc.usage = TextureUsage::Storage | TextureUsage::TransferDst;
    desc.debugName = "BindlessDummyStorage";
    desc.skipBindless = true;
    m_dummyStorageImage =
        m_rhiDevice->createTexture(desc.debugName.c_str(), desc);
  }
  {
    BufferDescriptor desc{};
    desc.size = 256;
    desc.usage = BufferUsage::StorageBuffer | BufferUsage::TransferDst;
    desc.debugName = "BindlessDummyBuffer";
    m_dummyBuffer = m_rhiDevice->createBuffer("BindlessDummyBuffer", desc);
  }
  {
    m_dummySampler = m_rhiDevice->createSampler(
        Filter::Nearest, Filter::Nearest, SamplerAddressMode::ClampToEdge);
  }

  auto props = m_physicalDevice.getProperties();
  uint32_t maxSampledImages = std::min(
      MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorSampledImages);
  uint32_t maxStorageImages = std::min(
      MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorStorageImages);
  uint32_t maxStorageBuffers = std::min(
      MAX_BINDLESS_RESOURCES, props.limits.maxPerStageDescriptorStorageBuffers);
  uint32_t maxSamplers =
      std::min(MAX_SAMPLERS, props.limits.maxPerStageDescriptorSamplers);

  m_textureManager.init(maxSampledImages);
  m_samplerManager.init(maxSamplers);
  m_shadowTextureManager.init(maxSampledImages);
  m_shadowSamplerManager.init(maxSamplers);
  m_bufferManager.init(maxStorageBuffers);
  m_cubemapManager.init(maxSampledImages);
  m_storageImageManager.init(maxStorageImages);
  m_msaaTextureManager.init(maxSampledImages);

  std::array<vk::DescriptorSetLayoutBinding, 9> bindings{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = vk::DescriptorType::eSampledImage;
  bindings[0].descriptorCount = maxSampledImages;
  bindings[0].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[1].binding = 1;
  bindings[1].descriptorType = vk::DescriptorType::eSampler;
  bindings[1].descriptorCount = maxSamplers;
  bindings[1].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[2].binding = 2;
  bindings[2].descriptorType = vk::DescriptorType::eSampledImage;
  bindings[2].descriptorCount = maxSampledImages;
  bindings[2].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[3].binding = 3;
  bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
  bindings[3].descriptorCount = maxStorageBuffers;
  bindings[3].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[4].binding = 4;
  bindings[4].descriptorType = vk::DescriptorType::eStorageImage;
  bindings[4].descriptorCount = maxStorageImages;
  bindings[4].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[5].binding = 5;
  bindings[5].descriptorType = vk::DescriptorType::eSampledImage;
  bindings[5].descriptorCount = maxSampledImages;
  bindings[5].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[6].binding = 6;
  bindings[6].descriptorType = vk::DescriptorType::eSampler;
  bindings[6].descriptorCount = maxSamplers;
  bindings[6].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[7].binding = 7;
  bindings[7].descriptorType = vk::DescriptorType::eSampledImage;
  bindings[7].descriptorCount = maxSampledImages;
  bindings[7].stageFlags = vk::ShaderStageFlagBits::eAll;

  bindings[8].binding = 8;
  bindings[8].descriptorType = vk::DescriptorType::eSampledImage;
  bindings[8].descriptorCount = maxSampledImages;
  bindings[8].stageFlags = vk::ShaderStageFlagBits::eAll;

  std::array<vk::DescriptorBindingFlags, 9> bindingFlags{};
  for (auto &flag : bindingFlags) {
    flag = vk::DescriptorBindingFlagBits::ePartiallyBound |
           vk::DescriptorBindingFlagBits::eUpdateAfterBind;
  }

  vk::DescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
  extendedInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
  extendedInfo.pBindingFlags = bindingFlags.data();

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.pNext = &extendedInfo;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  layoutInfo.flags =
      vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

  vk::DescriptorSetLayout vkLayout =
      m_device.createDescriptorSetLayout(layoutInfo);

  DescriptorSetLayout layoutDesc;
  layoutDesc.bindings.push_back({.binding = 0,
                                 .type = DescriptorType::SampledImage,
                                 .count = maxSampledImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessTextures"});
  layoutDesc.bindings.push_back({.binding = 1,
                                 .type = DescriptorType::Sampler,
                                 .count = maxSamplers,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessSamplers"});
  layoutDesc.bindings.push_back({.binding = 2,
                                 .type = DescriptorType::SampledImage,
                                 .count = maxSampledImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessCubemaps"});
  layoutDesc.bindings.push_back({.binding = 3,
                                 .type = DescriptorType::StorageBuffer,
                                 .count = maxStorageBuffers,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessStorageBuffers"});
  layoutDesc.bindings.push_back({.binding = 4,
                                 .type = DescriptorType::StorageImage,
                                 .count = maxStorageImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessStorageImages"});
  layoutDesc.bindings.push_back({.binding = 5,
                                 .type = DescriptorType::SampledImage,
                                 .count = maxSampledImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessTextures3D"});
  layoutDesc.bindings.push_back({.binding = 6,
                                 .type = DescriptorType::Sampler,
                                 .count = maxSamplers,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessSamplersShadow"});
  layoutDesc.bindings.push_back({.binding = 7,
                                 .type = DescriptorType::SampledImage,
                                 .count = maxSampledImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessTexturesShadow"});
  layoutDesc.bindings.push_back({.binding = 8,
                                 .type = DescriptorType::SampledImage,
                                 .count = maxSampledImages,
                                 .stages = ShaderStage::All,
                                 .name = "bindlessMSTextures"});

  m_bindlessLayout = std::make_unique<VulkanRHIDescriptorSetLayout>(
      m_device, vkLayout, layoutDesc);

  std::array<vk::DescriptorPoolSize, 4> poolSizes{};
  poolSizes[0].type = vk::DescriptorType::eSampledImage;
  poolSizes[0].descriptorCount = maxSampledImages * 5;
  poolSizes[1].type = vk::DescriptorType::eSampler;
  poolSizes[1].descriptorCount = maxSamplers * 2;
  poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
  poolSizes[2].descriptorCount = maxStorageBuffers;
  poolSizes[3].type = vk::DescriptorType::eStorageImage;
  poolSizes[3].descriptorCount = maxStorageImages;

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  m_bindlessPool = m_device.createDescriptorPool(poolInfo);

  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.descriptorPool = m_bindlessPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &vkLayout;

  m_bindlessSet = m_device.allocateDescriptorSets(allocInfo)[0];

  m_bindlessSetWrapper = std::make_unique<VulkanRHIDescriptorSet>(
      m_device, m_bindlessLayout.get(), m_bindlessSet);
}

void BindlessDescriptorManager::updateSampler(TextureBindlessHandle imageHandle,
                                              RHISampler *sampler) {
  if (sampler != nullptr && imageHandle.isValid()) {
    auto *vkSamp = rhi_cast<VulkanRHISampler>(sampler);
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = vkSamp->sampler();

    vk::WriteDescriptorSet write{};
    write.dstSet = m_bindlessSet;
    write.dstBinding = 1;
    write.dstArrayElement = imageHandle.index();
    write.descriptorType = vk::DescriptorType::eSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    m_device.updateDescriptorSets(write, nullptr);
  }
}

void BindlessDescriptorManager::updateTexture(TextureBindlessHandle handle,
                                              RHITexture *texture) {
  if (!handle.isValid() || (texture == nullptr)) {
    return;
  }

  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;

  switch (texture->type()) {
  case TextureType::TextureCube:
    write.dstBinding = 2;
    break;
  case TextureType::Texture3D:
    write.dstBinding = 5;
    break;
  default:
    if (texture->sampleCount() > 1) {
      write.dstBinding = 8;
    } else {
      write.dstBinding = 0;
    }
    break;
  }

  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  if (handle.isValid()) {
    BindlessSlotInfo info;
    info.name = texture->debugName();
    info.width = texture->extent().width;
    info.height = texture->extent().height;
    info.format = texture->format();

    switch (texture->type()) {
    case TextureType::TextureCube:
      m_cubemapManager.markOccupied(handle.index(), info);
      break;
    default:
      if (texture->sampleCount() > 1) {
        m_msaaTextureManager.markOccupied(handle.index(), info);
      } else {
        m_textureManager.markOccupied(handle.index(), info);
      }
      break;
    }
  }
}

TextureBindlessHandle
BindlessDescriptorManager::registerTexture(RHITexture *texture,
                                           RHISampler *sampler) {
  auto imageHandle = registerTexture2D(texture);
  updateSampler(imageHandle, sampler);
  return imageHandle;
}

TextureBindlessHandle
BindlessDescriptorManager::registerCubemap(RHITexture *texture,
                                           RHISampler *sampler) {
  auto imageHandle = registerCubemapImage(texture);
  updateSampler(imageHandle, sampler);
  return imageHandle;
}

TextureBindlessHandle
BindlessDescriptorManager::registerTexture2D(RHITexture *texture) {
  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);
  uint32_t index = m_textureManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return TextureBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 0;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = texture->debugName();
  info.width = texture->extent().width;
  info.height = texture->extent().height;
  info.format = texture->format();
  m_textureManager.markOccupied(index, info);

  return TextureBindlessHandle(index);
}

TextureBindlessHandle
BindlessDescriptorManager::registerCubemapImage(RHITexture *texture) {
  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);
  uint32_t index = m_cubemapManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return TextureBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 2;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = texture->debugName();
  info.width = texture->extent().width;
  info.height = texture->extent().height;
  info.format = texture->format();
  m_cubemapManager.markOccupied(index, info);

  return TextureBindlessHandle(index);
}

SamplerBindlessHandle
BindlessDescriptorManager::registerSampler(RHISampler *sampler) {
  auto *vkSamp = rhi_cast<VulkanRHISampler>(sampler);
  uint32_t index = m_samplerManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return SamplerBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler = vkSamp->sampler();

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 1;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampler;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = "Sampler";
  m_samplerManager.markOccupied(index, info);

  return SamplerBindlessHandle(index);
}

SamplerBindlessHandle
BindlessDescriptorManager::registerShadowSampler(RHISampler *sampler) {
  auto *vkSamp = rhi_cast<VulkanRHISampler>(sampler);
  uint32_t index = m_shadowSamplerManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return SamplerBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler = vkSamp->sampler();

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 6;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampler;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = "ShadowSampler";
  m_shadowSamplerManager.markOccupied(index, info);

  return SamplerBindlessHandle(index);
}

BufferBindlessHandle
BindlessDescriptorManager::registerBuffer(RHIBuffer *buffer) {
  auto *vkBuf = rhi_cast<VulkanRHIBuffer>(buffer);
  uint32_t index = m_bufferManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return BufferBindlessHandle::Invalid;
  }

  vk::DescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = vkBuf->buffer();
  bufferInfo.offset = 0;
  bufferInfo.range = VK_WHOLE_SIZE;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 3;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eStorageBuffer;
  write.descriptorCount = 1;
  write.pBufferInfo = &bufferInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = buffer->debugName();
  info.width = (uint32_t)buffer->size();
  m_bufferManager.markOccupied(index, info);

  return BufferBindlessHandle(index);
}

TextureBindlessHandle
BindlessDescriptorManager::registerStorageImage(RHITexture *texture) {
  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);
  uint32_t index = m_storageImageManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return TextureBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eGeneral;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());
  imageInfo.sampler = nullptr;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 4;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eStorageImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = texture->debugName();
  info.width = texture->extent().width;
  info.height = texture->extent().height;
  info.format = texture->format();
  m_storageImageManager.markOccupied(index, info);

  return TextureBindlessHandle(index);
}

TextureBindlessHandle
BindlessDescriptorManager::registerShadowTexture2D(RHITexture *texture) {
  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);
  uint32_t index = m_shadowTextureManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return TextureBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 7;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = texture->debugName();
  info.width = texture->extent().width;
  info.height = texture->extent().height;
  info.format = texture->format();
  m_shadowTextureManager.markOccupied(index, info);

  return TextureBindlessHandle(index);
}

TextureBindlessHandle
BindlessDescriptorManager::registerMSTexture2D(RHITexture *texture) {
  auto *vkTex = rhi_cast<VulkanRHITexture>(texture);
  uint32_t index = m_msaaTextureManager.allocate();
  if (index == kInvalidBindlessIndex) {
    return TextureBindlessHandle::Invalid;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(vkTex->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 8;
  write.dstArrayElement = index;
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  m_device.updateDescriptorSets(write, nullptr);

  BindlessSlotInfo info;
  info.name = texture->debugName();
  info.width = texture->extent().width;
  info.height = texture->extent().height;
  info.format = texture->format();
  m_msaaTextureManager.markOccupied(index, info);

  return TextureBindlessHandle(index);
}

void BindlessDescriptorManager::update(uint64_t completedFrame) {
  m_textureManager.update(completedFrame);
  m_samplerManager.update(completedFrame);
  m_shadowTextureManager.update(completedFrame);
  m_shadowSamplerManager.update(completedFrame);
  m_bufferManager.update(completedFrame);
  m_cubemapManager.update(completedFrame);
  m_storageImageManager.update(completedFrame);
  m_msaaTextureManager.update(completedFrame);
}

void BindlessDescriptorManager::releaseTexture(TextureBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(
      rhi_cast<VulkanRHITexture>(m_dummyTexture.get())->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 0;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_textureManager.freeDeferred(handle.index(), m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseCubemap(TextureBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(
      rhi_cast<VulkanRHITexture>(m_dummyCube.get())->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 2;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_cubemapManager.freeDeferred(handle.index(), m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseSampler(SamplerBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler =
      rhi_cast<VulkanRHISampler>(m_dummySampler.get())->sampler();

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 1;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampler;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_samplerManager.freeDeferred(handle.index(), m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseShadowSampler(
    SamplerBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.sampler =
      rhi_cast<VulkanRHISampler>(m_dummySampler.get())->sampler();

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 6;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampler;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_shadowSamplerManager.freeDeferred(handle.index(),
                                      m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseStorageImage(
    TextureBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eGeneral;
  imageInfo.imageView = vk::ImageView(
      rhi_cast<VulkanRHITexture>(m_dummyStorageImage.get())->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 4;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eStorageImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_storageImageManager.freeDeferred(handle.index(),
                                     m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseBuffer(BufferBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = rhi_cast<VulkanRHIBuffer>(m_dummyBuffer.get())->buffer();
  bufferInfo.offset = 0;
  bufferInfo.range = VK_WHOLE_SIZE;

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 3;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eStorageBuffer;
  write.descriptorCount = 1;
  write.pBufferInfo = &bufferInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_bufferManager.freeDeferred(handle.index(), m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseShadowTexture2D(
    TextureBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(
      rhi_cast<VulkanRHITexture>(m_dummyTexture.get())->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 7;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_shadowTextureManager.freeDeferred(handle.index(),
                                      m_rhiDevice->getCurrentFrame());
}

void BindlessDescriptorManager::releaseMSTexture2D(
    TextureBindlessHandle handle) {
  if (!handle.isValid()) {
    return;
  }

  vk::DescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.imageView = vk::ImageView(
      rhi_cast<VulkanRHITexture>(m_dummyTexture.get())->imageViewHandle());

  vk::WriteDescriptorSet write{};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 8;
  write.dstArrayElement = handle.index();
  write.descriptorType = vk::DescriptorType::eSampledImage;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  m_device.updateDescriptorSets(write, nullptr);

  m_msaaTextureManager.freeDeferred(handle.index(),
                                    m_rhiDevice->getCurrentFrame());
}

BindlessStatistics BindlessDescriptorManager::getStatistics() const {
  std::scoped_lock lock(m_mutex);
  BindlessStatistics stats;

  auto addArray = [&](const std::string &name,
                      const BindlessResourceManager &manager) {
    BindlessStatistics::ArrayStats array;
    array.name = name;
    array.capacity = manager.maxCapacity();
    array.freeListSize = manager.freeListSize();
    uint32_t count = 0;
    const auto &slots = manager.slots();
    for (const auto &slot : slots) {
      if (slot.isOccupied) {
        count++;
      }
    }
    array.occupied = count;
    array.slots.reserve(count);
    for (uint32_t i = 0; i < slots.size(); ++i) {
      if (slots[i].isOccupied) {
        array.slots.push_back(slots[i]);
        array.slots.back().slotIndex = i;
      }
    }
    stats.arrays.push_back(std::move(array));
  };

  addArray("Textures2D", m_textureManager);
  addArray("Samplers", m_samplerManager);
  addArray("Cubemaps", m_cubemapManager);
  addArray("StorageBuffers", m_bufferManager);
  addArray("StorageImages", m_storageImageManager);
  addArray("SamplersShadow", m_shadowSamplerManager);
  addArray("TexturesShadow", m_shadowTextureManager);
  addArray("MSTextures", m_msaaTextureManager);

  return stats;
}

RHIDescriptorSet *BindlessDescriptorManager::getDescriptorSet() const {
  return static_cast<RHIDescriptorSet *>(m_bindlessSetWrapper.get());
}

RHIDescriptorSetLayout *
BindlessDescriptorManager::getDescriptorSetLayout() const {
  return static_cast<RHIDescriptorSetLayout *>(m_bindlessLayout.get());
}

} // namespace pnkr::renderer::rhi::vulkan
