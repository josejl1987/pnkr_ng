#pragma once

#include "pnkr/core/logger.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_descriptor.hpp"
#include "pnkr/rhi/rhi_pipeline.hpp"
#include "pnkr/rhi/rhi_sampler.hpp"
#include "pnkr/rhi/rhi_sync.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include <pnkr/rhi/rhi_device.hpp>

namespace pnkr::renderer::rhi {

class NullBindlessManager : public BindlessManager {
public:
  TextureBindlessHandle registerTexture(RHITexture * /*texture*/,
                                        RHISampler * /*sampler*/) override {
    return TextureBindlessHandle(0);
  }
  TextureBindlessHandle registerCubemap(RHITexture * /*texture*/,
                                        RHISampler * /*sampler*/) override {
    return TextureBindlessHandle(0);
  }
  TextureBindlessHandle registerTexture2D(RHITexture * /*texture*/) override {
    return TextureBindlessHandle(0);
  }
  TextureBindlessHandle
  registerCubemapImage(RHITexture * /*texture*/) override {
    return TextureBindlessHandle(0);
  }
  SamplerBindlessHandle registerSampler(RHISampler * /*sampler*/) override {
    return SamplerBindlessHandle(0);
  }
  SamplerBindlessHandle
  registerShadowSampler(RHISampler * /*sampler*/) override {
    return SamplerBindlessHandle(0);
  }
  TextureBindlessHandle
  registerStorageImage(RHITexture * /*texture*/) override {
    return TextureBindlessHandle(0);
  }
  BufferBindlessHandle registerBuffer(RHIBuffer * /*buffer*/) override {
    return BufferBindlessHandle(0);
  }
  TextureBindlessHandle
  registerShadowTexture2D(RHITexture * /*texture*/) override {
    return TextureBindlessHandle(0);
  }
  TextureBindlessHandle registerMSTexture2D(RHITexture * /*texture*/) override {
    return TextureBindlessHandle(0);
  }

  void updateTexture(TextureBindlessHandle /*handle*/,
                     RHITexture * /*texture*/) override {}

  void releaseTexture(TextureBindlessHandle /*handle*/) override {}
  void releaseCubemap(TextureBindlessHandle /*handle*/) override {}
  void releaseSampler(SamplerBindlessHandle /*handle*/) override {}
  void releaseShadowSampler(SamplerBindlessHandle /*handle*/) override {}
  void releaseStorageImage(TextureBindlessHandle /*handle*/) override {}
  void releaseBuffer(BufferBindlessHandle /*handle*/) override {}
  void releaseShadowTexture2D(TextureBindlessHandle /*handle*/) override {}
  void releaseMSTexture2D(TextureBindlessHandle /*handle*/) override {}
};

class NullRHIBuffer : public RHIBuffer {
public:
  NullRHIBuffer(const BufferDescriptor &desc) {
    m_size = desc.size;
    m_usage = desc.usage;
    m_memoryUsage = desc.memoryUsage;
    m_debugName = desc.debugName;

    if (m_size > 0) {
      m_storage.resize(m_size);
      std::memset(m_storage.data(), 0, m_size);
    }

    pnkr::core::Logger::RHI.trace("NullRHIBuffer created: {} (size: {})",
                                  m_debugName, m_size);
  }

  std::byte *map() override {
    if (m_storage.empty())
      return nullptr;
    return m_storage.data();
  }
  void unmap() override {}
  void flush(uint64_t /*offset*/, uint64_t /*size*/) override {}
  void invalidate(uint64_t /*offset*/, uint64_t /*size*/) override {}
  void uploadData(std::span<const std::byte> data, uint64_t offset) override {
    if (offset + data.size() <= m_storage.size()) {
      std::memcpy(m_storage.data() + offset, data.data(), data.size());
    }
  }

  uint64_t size() const override { return m_size; }
  BufferUsageFlags usage() const override { return m_usage; }
  MemoryUsage memoryUsage() const override { return m_memoryUsage; }

  void *nativeHandle() const override { return (void *)m_storage.data(); }
  uint64_t getDeviceAddress() const override {
    if (m_storage.empty())
      return 0;
    return reinterpret_cast<uint64_t>(m_storage.data());
  }

private:
  uint64_t m_size;
  BufferUsageFlags m_usage;
  MemoryUsage m_memoryUsage;
  std::vector<std::byte> m_storage;
};

class NullRHITexture : public RHITexture {
public:
  NullRHITexture(const TextureDescriptor &desc) {
    m_extent = desc.extent;
    m_format = desc.format;
    m_mipLevels = desc.mipLevels;
    m_arrayLayers = desc.arrayLayers;
    m_sampleCount = desc.sampleCount;
    m_usage = desc.usage;
    m_debugName = desc.debugName;
    m_type = desc.type;
    pnkr::core::Logger::RHI.trace("NullRHITexture created: {}", m_debugName);
  }

  NullRHITexture(const TextureViewDescriptor &desc, RHITexture *parent) {
    m_extent = parent->extent();
    m_format =
        desc.format == Format::Undefined ? parent->format() : desc.format;
    m_mipLevels = desc.mipCount;
    m_arrayLayers = desc.layerCount;
    m_sampleCount = parent->sampleCount();
    m_usage = parent->usage();
    m_debugName = desc.debugName;
    m_type = parent->type();
    pnkr::core::Logger::RHI.trace("NullRHITexture View created: {}",
                                  m_debugName);
  }

  void uploadData(std::span<const std::byte> /*data*/,
                  const TextureSubresource & /*subresource*/) override {}
  void generateMipmaps() override {}
  void generateMipmaps(RHICommandList * /*cmd*/) override {}

  const Extent3D &extent() const override { return m_extent; }
  Format format() const override { return m_format; }
  uint32_t mipLevels() const override { return m_mipLevels; }
  uint32_t arrayLayers() const override { return m_arrayLayers; }
  uint32_t sampleCount() const override { return m_sampleCount; }
  TextureUsageFlags usage() const override { return m_usage; }

  void *nativeHandle() const override { return (void *)this; }
  void *nativeView() const override { return (void *)this; }
  void *nativeView(uint32_t /*mipLevel*/,
                   uint32_t /*arrayLayer*/) const override {
    return (void *)this;
  }

private:
  Extent3D m_extent;
  Format m_format;
  uint32_t m_mipLevels;
  uint32_t m_arrayLayers;
  uint32_t m_sampleCount;
  TextureUsageFlags m_usage;
};

class NullRHISampler : public RHISampler {
public:
  void *nativeHandle() const override { return (void *)this; }
};

class NullRHIPipeline : public RHIPipeline {
public:
  NullRHIPipeline(PipelineBindPoint bindPoint) : m_bindPoint(bindPoint) {}
  PipelineBindPoint bindPoint() const override { return m_bindPoint; }
  void *nativeHandle() const override { return (void *)this; }
  RHIDescriptorSetLayout *
  descriptorSetLayout(uint32_t /*setIndex*/) const override {
    return nullptr;
  }
  uint32_t descriptorSetLayoutCount() const override { return 0; }

private:
  PipelineBindPoint m_bindPoint;
};

class NullRHIDescriptorSetLayout : public RHIDescriptorSetLayout {
public:
  void *nativeHandle() const override { return (void *)this; }
  const DescriptorSetLayout &description() const override {
    static DescriptorSetLayout d;
    return d;
  }
};

class NullRHIDescriptorSet : public RHIDescriptorSet {
public:
  void updateBuffer(uint32_t /*binding*/, RHIBuffer * /*buffer*/,
                    uint64_t /*offset*/, uint64_t /*range*/) override {}
  void updateTexture(uint32_t /*binding*/, RHITexture * /*texture*/,
                     RHISampler * /*sampler*/) override {}
  void *nativeHandle() const override { return (void *)this; }
};

class NullRHIFence : public RHIFence {
public:
  NullRHIFence(bool signaled) : m_signaled(signaled) {}
  void reset() override { m_signaled = false; }
  bool wait(uint64_t /*timeout*/) override { return true; }
  bool isSignaled() const override { return m_signaled; }
  void *nativeHandle() const override { return (void *)this; }
  void signal() { m_signaled = true; }

private:
  bool m_signaled;
};

struct NullRHICommandPool : public RHICommandPool {
  void reset() override {}
  void *nativeHandle() override { return (void *)this; }
};

class NullRHICommandBuffer : public RHICommandBuffer {
public:
  void setProfilingContext(void * /*ctx*/) override {}
  void *getProfilingContext() const override { return nullptr; }
  void resolveTexture(RHITexture * /*src*/, ResourceLayout /*srcLayout*/,
                      RHITexture * /*dst*/, ResourceLayout /*dstLayout*/,
                      const TextureCopyRegion & /*region*/) override {}
  void begin() override {}
  void end() override {}
  void reset() override {}
  void beginRendering(const RenderingInfo & /*info*/) override {}
  void endRendering() override {}
  void bindPipeline(RHIPipeline *pipeline) override { m_pipeline = pipeline; }
  void bindVertexBuffer(uint32_t /*binding*/, RHIBuffer * /*buffer*/,
                        uint64_t /*offset*/) override {}
  void bindIndexBuffer(RHIBuffer * /*buffer*/, uint64_t /*offset*/,
                       bool /*use16Bit*/) override {}
  void draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/,
            uint32_t /*firstVertex*/, uint32_t /*firstInstance*/) override {}
  void drawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/,
                   uint32_t /*firstIndex*/, int32_t /*vertexOffset*/,
                   uint32_t /*firstInstance*/) override {}
  void drawIndexedIndirect(RHIBuffer * /*buffer*/, uint64_t /*offset*/,
                           uint32_t /*drawCount*/,
                           uint32_t /*stride*/) override {}
  void drawIndexedIndirectCount(RHIBuffer * /*buffer*/, uint64_t /*offset*/,
                                RHIBuffer * /*countBuffer*/,
                                uint64_t /*countBufferOffset*/,
                                uint32_t /*maxDrawCount*/,
                                uint32_t /*stride*/) override {}
  void dispatch(uint32_t /*groupCountX*/, uint32_t /*groupCountY*/,
                uint32_t /*groupCountZ*/) override {}
  void pushConstants(RHIPipeline * /*pipeline*/, ShaderStageFlags /*stages*/,
                     uint32_t /*offset*/, uint32_t /*size*/,
                     const void * /*data*/) override {}
  void bindDescriptorSet(RHIPipeline * /*pipeline*/, uint32_t /*setIndex*/,
                         RHIDescriptorSet * /*descriptorSet*/) override {}
  void setViewport(const Viewport & /*viewport*/) override {}
  void setScissor(const Rect2D & /*scissor*/) override {}
  void setDepthBias(float /*constantFactor*/, float /*clamp*/,
                    float /*slopeFactor*/) override {}
  void setCullMode(CullMode /*mode*/) override {}
  void setDepthTestEnable(bool /*enable*/) override {}
  void setDepthWriteEnable(bool /*enable*/) override {}
  void setDepthCompareOp(CompareOp /*op*/) override {}
  void setPrimitiveTopology(PrimitiveTopology /*topology*/) override {}
  void
  pipelineBarrier(ShaderStageFlags /*srcStage*/, ShaderStageFlags /*dstStage*/,
                  std::span<const RHIMemoryBarrier> /*barriers*/) override {}
  void copyBuffer(RHIBuffer *src, RHIBuffer *dst, uint64_t srcOffset,
                  uint64_t dstOffset, uint64_t size) override {
    auto *srcNull = static_cast<NullRHIBuffer *>(src);
    auto *dstNull = static_cast<NullRHIBuffer *>(dst);
    auto *srcPtr = srcNull->map();
    auto *dstPtr = dstNull->map();
    if (srcPtr && dstPtr) {
      std::memcpy(dstPtr + dstOffset, srcPtr + srcOffset, size);
    }
  }
  void fillBuffer(RHIBuffer * /*buffer*/, uint64_t /*offset*/,
                  uint64_t /*size*/, uint32_t /*data*/) override {}
  void
  copyBufferToTexture(RHIBuffer * /*src*/, RHITexture * /*dst*/,
                      const BufferTextureCopyRegion & /*region*/) override {}
  void copyBufferToTexture(
      RHIBuffer * /*src*/, RHITexture * /*dst*/,
      std::span<const BufferTextureCopyRegion> /*regions*/) override {}
  void
  copyTextureToBuffer(RHITexture * /*src*/, RHIBuffer * /*dst*/,
                      const BufferTextureCopyRegion & /*region*/) override {}
  void copyTexture(RHITexture * /*src*/, RHITexture * /*dst*/,
                   const TextureCopyRegion & /*region*/) override {}
  void blitTexture(RHITexture * /*src*/, RHITexture * /*dst*/,
                   const TextureBlitRegion & /*region*/,
                   Filter /*filter*/) override {}
  void clearImage(RHITexture * /*texture*/, const ClearValue & /*clearValue*/,
                  ResourceLayout /*layout*/) override {}
  void beginDebugLabel(const char * /*name*/, float /*r*/, float /*g*/,
                       float /*b*/, float /*a*/) override {}
  void endDebugLabel() override {}
  void insertDebugLabel(const char * /*name*/, float /*r*/, float /*g*/,
                        float /*b*/, float /*a*/) override {}
  void pushGPUMarker(const char * /*name*/) override {}
  void popGPUMarker() override {}
  void *nativeHandle() const override { return (void *)this; }

protected:
  RHIPipeline *boundPipeline() const override { return m_pipeline; }
  void pushConstantsInternal(ShaderStageFlags /*stages*/, uint32_t /*offset*/,
                             uint32_t /*size*/,
                             const void * /*data*/) override {}

private:
  RHIPipeline *m_pipeline = nullptr;
};

} // namespace pnkr::renderer::rhi
