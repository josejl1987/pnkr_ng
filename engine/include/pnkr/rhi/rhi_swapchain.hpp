#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include <cstdint>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi
{
    class RHITexture;

    struct SwapchainFrame
    {
        uint32_t imageIndex = 0;
        RHITexture* color = nullptr;
    };

    class RHISwapchain
    {
    public:
        virtual ~RHISwapchain() = default;

        virtual Format colorFormat() const = 0;
        virtual Extent2D extent() const = 0;
        virtual uint32_t imageCount() const = 0;
        virtual uint32_t framesInFlight() const = 0;

        virtual bool beginFrame(uint32_t frameIndex, RHICommandList* cmd, SwapchainFrame& out) = 0;

        virtual bool endFrame(uint32_t frameIndex, RHICommandList* cmd) = 0;

        virtual bool present(uint32_t frameIndex) = 0;

        virtual void recreate(uint32_t width, uint32_t height) = 0;

        virtual void setVsync(bool enabled) = 0;

        virtual ResourceLayout currentLayout() const { return ResourceLayout::Undefined; }

        virtual void* getProfilingContext() const { return nullptr; }
    };
}
