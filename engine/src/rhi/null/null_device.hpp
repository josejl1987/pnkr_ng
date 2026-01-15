#pragma once

#include "null_resources.hpp"
#include "pnkr/rhi/rhi_device.hpp"

namespace pnkr::renderer::rhi {
class NullRHIPhysicalDevice : public RHIPhysicalDevice {
public:
  NullRHIPhysicalDevice() {
    m_capabilities.deviceName = "Null RHI Device";
    m_capabilities.vendorID = 0;
    m_capabilities.deviceID = 0;
    m_capabilities.discreteGPU = true;
    m_capabilities.maxImageDimension2D = 16384;
    m_capabilities.maxImageDimension3D = 2048;
    m_capabilities.maxFramebufferWidth = 16384;
    m_capabilities.maxFramebufferHeight = 16384;
    m_capabilities.maxColorAttachments = 8;
    m_capabilities.geometryShader = true;
    m_capabilities.tessellationShader = true;
    m_capabilities.samplerAnisotropy = true;
    m_capabilities.textureCompressionBC = true;
    m_capabilities.bindlessTextures = true;
    m_capabilities.drawIndirectCount = true;
    m_capabilities.pipelineStatisticsQuery = true;
    m_capabilities.rayTracing = true;
    m_capabilities.meshShading = true;
  }

  const DeviceCapabilities &capabilities() const override {
    return m_capabilities;
  }
  std::vector<QueueFamilyInfo> queueFamilies() const override {
    return {{0, 1, true, true, true}};
  }
  bool
  supportsPresentation([[maybe_unused]] uint32_t queueFamily) const override {
    return true;
  }

private:
  DeviceCapabilities m_capabilities{};
};

class NullRHIDevice : public RHIDevice {
public:
  NullRHIDevice(std::unique_ptr<NullRHIPhysicalDevice> physicalDevice);
  ~NullRHIDevice() override;

  std::unique_ptr<RHIBuffer>
  createBuffer(const char *name, const BufferDescriptor &desc) override;
  std::unique_ptr<RHITexture>
  createTexture(const char *name, const TextureDescriptor &desc) override;
  std::unique_ptr<RHITexture>
  createTextureView(const char *name, RHITexture *parent,
                    const TextureViewDescriptor &desc) override;

  std::unique_ptr<RHITexture>
  createTexture(const Extent3D &extent, Format format, TextureUsageFlags usage,
                uint32_t mipLevels, uint32_t arrayLayers) override;
  std::unique_ptr<RHITexture> createCubemap(const Extent3D &extent,
                                            Format format,
                                            TextureUsageFlags usage,
                                            uint32_t mipLevels) override;

  std::unique_ptr<RHISampler> createSampler(Filter minFilter, Filter magFilter,
                                            SamplerAddressMode addressMode,
                                            CompareOp compareOp) override;
  std::unique_ptr<RHICommandPool>
  createCommandPool(const CommandPoolDescriptor &desc) override;
  std::unique_ptr<RHICommandBuffer>
  createCommandBuffer(RHICommandPool *pool) override;

  std::unique_ptr<RHIPipeline>
  createGraphicsPipeline(const GraphicsPipelineDescriptor &desc) override;
  std::unique_ptr<RHIPipeline>
  createComputePipeline(const ComputePipelineDescriptor &desc) override;

  std::unique_ptr<RHIUploadContext>
  createUploadContext(uint64_t stagingBufferSize) override;

  std::unique_ptr<RHIDescriptorSetLayout>
  createDescriptorSetLayout(const DescriptorSetLayout &desc) override;
  std::unique_ptr<RHIDescriptorSet>
  allocateDescriptorSet(RHIDescriptorSetLayout *layout) override;

  std::unique_ptr<RHIFence> createFence(bool signaled) override;
  void waitIdle() override {}
  void waitForFences(
      [[maybe_unused]] const std::vector<uint64_t> &fenceValues) override {}

  void waitForFrame([[maybe_unused]] uint64_t frameIndex) override {}
  uint64_t incrementFrame() override { return ++m_frameIndex; }
  uint64_t getCompletedFrame() const override { return m_frameIndex; }

  void submitCommands(RHICommandList *commandBuffer, RHIFence *signalFence,
                      const std::vector<uint64_t> &waitSemaphores,
                      const std::vector<uint64_t> &signalSemaphores,
                      RHISwapchain *swapchain) override;
  void
  submitComputeCommands([[maybe_unused]] RHICommandList *commandBuffer,
                        [[maybe_unused]] bool waitForPreviousCompute,
                        [[maybe_unused]] bool signalGraphicsQueue) override {}

  uint64_t getLastComputeSemaphoreValue() const override { return 0; }

  void immediateSubmit(std::function<void(RHICommandList *)> &&func) override;

  void downloadTexture(
      [[maybe_unused]] RHITexture *texture,
      [[maybe_unused]] std::span<std::byte> outData,
      [[maybe_unused]] const TextureSubresource &subresource) override {}

  const RHIPhysicalDevice &physicalDevice() const override {
    return *m_physicalDevice;
  }
  uint32_t graphicsQueueFamily() const override { return 0; }
  uint32_t computeQueueFamily() const override { return 0; }
  uint32_t transferQueueFamily() const override { return 0; }

  uint32_t getMaxUsableSampleCount() const override { return 1; }

  BindlessManager *getBindlessManager() override;

  std::unique_ptr<RHIImGui> createImGuiRenderer() override;

  void clearPipelineCache() override {}
  size_t getPipelineCacheSize() const override { return 0; }

  void auditBDA([[maybe_unused]] uint64_t address,
                [[maybe_unused]] const char *context) override {}

  RHIDescriptorSet *getBindlessDescriptorSet() override;
  RHIDescriptorSetLayout *getBindlessDescriptorSetLayout() override;
  void *getNativeInstance() const override { return nullptr; }

private:
  std::unique_ptr<NullRHIPhysicalDevice> m_physicalDevice;
  uint64_t m_frameIndex = 0;
};
} // namespace pnkr::renderer::rhi
