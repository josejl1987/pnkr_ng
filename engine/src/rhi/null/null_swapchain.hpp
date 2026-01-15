#pragma once

#include "null_device.hpp"
#include "pnkr/rhi/rhi_swapchain.hpp"

namespace pnkr::renderer::rhi {
class NullRHISwapchain : public RHISwapchain {
public:
  NullRHISwapchain(NullRHIDevice * /*device*/, Format format)
      : m_format(format) {
    TextureDescriptor desc{};
    desc.extent = {1280, 720, 1};
    desc.format = format;
    desc.usage = TextureUsage::ColorAttachment | TextureUsage::TransferSrc;

    for (int i = 0; i < 3; ++i) {
      m_images.push_back(std::make_unique<NullRHITexture>(desc));
    }
  }

  ~NullRHISwapchain() override = default;

  Format colorFormat() const override { return m_format; }
  Extent2D extent() const override { return {1280, 720}; }
  uint32_t imageCount() const override { return 3; }
  uint32_t framesInFlight() const override { return 3; }

  bool beginFrame(uint32_t frameIndex, RHICommandList * /*cmd*/,
                  SwapchainFrame &out) override {
    out.imageIndex = frameIndex % 3;
    out.color = m_images[out.imageIndex].get();
    return true;
  }

  bool endFrame(uint32_t /*frameIndex*/, RHICommandList * /*cmd*/) override {
    return true;
  }

  bool present(uint32_t /*frameIndex*/) override { return true; }

  void recreate(uint32_t /*width*/, uint32_t /*height*/) override {}

  void setVsync(bool /*enabled*/) override {}

private:
  Format m_format;
  std::vector<std::unique_ptr<NullRHITexture>> m_images;
};
} // namespace pnkr::renderer::rhi
