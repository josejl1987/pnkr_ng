#include "null_device.hpp"
#include "null_swapchain.hpp"
#include "pnkr/rhi/rhi_imgui.hpp"

namespace pnkr::renderer::rhi {

class NullRHIUploadContext : public RHIUploadContext {
public:
  void uploadTexture(RHITexture *texture, std::span<const std::byte> /*data*/,
                     const TextureSubresource & /*subresource*/) override {
    pnkr::core::Logger::RHI.trace("NullRHIUploadContext::uploadTexture: {}",
                                  texture->debugName());
  }
  void uploadBuffer(RHIBuffer *buffer, std::span<const std::byte> data,
                    uint64_t offset) override {
    pnkr::core::Logger::RHI.trace(
        "NullRHIUploadContext::uploadBuffer: {} (offset: {}, size: {})",
        buffer->debugName(), offset, data.size());
    auto *bufferNull = static_cast<NullRHIBuffer *>(buffer);
    auto *ptr = bufferNull->map();
    if (ptr) {
      std::memcpy(static_cast<std::byte *>(ptr) + offset, data.data(),
                  data.size());
    }
  }
  void flush() override {
    pnkr::core::Logger::RHI.trace("NullRHIUploadContext::flush");
  }
};

class NullRHIImGui : public RHIImGui {
public:
  void init(void * /*windowHandle*/, Format /*colorFormat*/,
            Format /*depthFormat*/, uint32_t /*framesInFlight*/) override {
    pnkr::core::Logger::RHI.trace("NullRHIImGui::init");
  }
  void shutdown() override {
    pnkr::core::Logger::RHI.trace("NullRHIImGui::shutdown");
  }
  void beginFrame(uint32_t frameIndex) override {
    pnkr::core::Logger::RHI.trace("NullRHIImGui::beginFrame: {}", frameIndex);
  }
  void renderDrawData(RHICommandList * /*cmd*/,
                      ImDrawData * /*drawData*/) override {
    pnkr::core::Logger::RHI.trace("NullRHIImGui::renderDrawData");
  }
  void *registerTexture(void * /*nativeTextureView*/,
                        void * /*nativeSampler*/) override {
    return (void *)0x1234;
  }
  void removeTexture(void * /*descriptorSet*/) override {
    pnkr::core::Logger::RHI.trace("NullRHIImGui::removeTexture");
  }
};

NullRHIDevice::NullRHIDevice(
    std::unique_ptr<NullRHIPhysicalDevice> physicalDevice)
    : m_physicalDevice(std::move(physicalDevice)) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice created");
}

NullRHIDevice::~NullRHIDevice() = default;

std::unique_ptr<RHIBuffer>
NullRHIDevice::createBuffer(const char *name, const BufferDescriptor &desc) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createBuffer: {}", name);
  return std::make_unique<NullRHIBuffer>(desc);
}

std::unique_ptr<RHITexture>
NullRHIDevice::createTexture(const char *name, const TextureDescriptor &desc) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createTexture: {}", name);
  return std::make_unique<NullRHITexture>(desc);
}

std::unique_ptr<RHITexture>
NullRHIDevice::createTextureView(const char *name, RHITexture *parent,
                                 const TextureViewDescriptor &desc) {
  pnkr::core::Logger::RHI.trace(
      "NullRHIDevice::createTextureView: {} (parent: {})", name,
      parent->debugName());
  return std::make_unique<NullRHITexture>(desc, parent);
}

std::unique_ptr<RHITexture>
NullRHIDevice::createTexture(const Extent3D &extent, Format format,
                             TextureUsageFlags usage, uint32_t mipLevels,
                             uint32_t arrayLayers) {
  TextureDescriptor desc{};
  desc.extent = extent;
  desc.format = format;
  desc.usage = usage;
  desc.mipLevels = mipLevels;
  desc.arrayLayers = arrayLayers;
  return createTexture("Texture", desc);
}

std::unique_ptr<RHITexture>
NullRHIDevice::createCubemap(const Extent3D &extent, Format format,
                             TextureUsageFlags usage, uint32_t mipLevels) {
  TextureDescriptor desc{};
  desc.extent = extent;
  desc.format = format;
  desc.usage = usage;
  desc.mipLevels = mipLevels;
  desc.arrayLayers = 6;
  desc.type = TextureType::TextureCube;
  return createTexture("Cubemap", desc);
}

std::unique_ptr<RHISampler>
NullRHIDevice::createSampler(Filter /*minFilter*/, Filter /*magFilter*/,
                             SamplerAddressMode /*addressMode*/,
                             CompareOp /*compareOp*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createSampler");
  return std::make_unique<NullRHISampler>();
}

std::unique_ptr<RHICommandPool>
NullRHIDevice::createCommandPool(const CommandPoolDescriptor & /*desc*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createCommandPool");
  return std::make_unique<NullRHICommandPool>();
}

std::unique_ptr<RHICommandBuffer>
NullRHIDevice::createCommandBuffer(RHICommandPool * /*pool*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createCommandBuffer");
  return std::make_unique<NullRHICommandBuffer>();
}

std::unique_ptr<RHIPipeline>
NullRHIDevice::createGraphicsPipeline(const GraphicsPipelineDescriptor &desc) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createGraphicsPipeline: {}",
                                desc.debugName);
  return std::make_unique<NullRHIPipeline>(PipelineBindPoint::Graphics);
}

std::unique_ptr<RHIPipeline>
NullRHIDevice::createComputePipeline(const ComputePipelineDescriptor &desc) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createComputePipeline: {}",
                                desc.debugName);
  return std::make_unique<NullRHIPipeline>(PipelineBindPoint::Compute);
}

std::unique_ptr<RHIUploadContext>
NullRHIDevice::createUploadContext(uint64_t stagingBufferSize) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createUploadContext (size: {})",
                                stagingBufferSize);
  return std::make_unique<NullRHIUploadContext>();
}

std::unique_ptr<RHIDescriptorSetLayout>
NullRHIDevice::createDescriptorSetLayout(const DescriptorSetLayout & /*desc*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createDescriptorSetLayout");
  return std::make_unique<NullRHIDescriptorSetLayout>();
}

std::unique_ptr<RHIDescriptorSet>
NullRHIDevice::allocateDescriptorSet(RHIDescriptorSetLayout * /*layout*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::allocateDescriptorSet");
  return std::make_unique<NullRHIDescriptorSet>();
}

std::unique_ptr<RHIFence> NullRHIDevice::createFence(bool signaled) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createFence (signaled: {})",
                                signaled);
  return std::make_unique<NullRHIFence>(signaled);
}

void NullRHIDevice::submitCommands(
    RHICommandList * /*commandBuffer*/, RHIFence *signalFence,
    const std::vector<uint64_t> & /*waitSemaphores*/,
    const std::vector<uint64_t> & /*signalSemaphores*/,
    RHISwapchain * /*swapchain*/) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::submitCommands");
  if (signalFence) {
    static_cast<NullRHIFence *>(signalFence)->signal();
  }
}

void NullRHIDevice::immediateSubmit(
    std::function<void(RHICommandList *)> &&func) {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::immediateSubmit");
  NullRHICommandBuffer cmd;
  func(&cmd);
}

std::unique_ptr<RHIImGui> NullRHIDevice::createImGuiRenderer() {
  pnkr::core::Logger::RHI.trace("NullRHIDevice::createImGuiRenderer");
  return std::make_unique<NullRHIImGui>();
}

BindlessManager *NullRHIDevice::getBindlessManager() {
  static NullBindlessManager s_manager;
  return &s_manager;
}

RHIDescriptorSet *NullRHIDevice::getBindlessDescriptorSet() {
  static NullRHIDescriptorSet s_set;
  return &s_set;
}

RHIDescriptorSetLayout *NullRHIDevice::getBindlessDescriptorSetLayout() {
  static NullRHIDescriptorSetLayout s_layout;
  return &s_layout;
}

} // namespace pnkr::renderer::rhi
