#pragma once

#include "pnkr/rhi/rhi_types.hpp"
#include <cstdint>

namespace pnkr::platform { class Window; }

namespace pnkr::renderer::rhi
{
    class RHICommandBuffer;
    class RHITexture;

    struct SwapchainFrame
    {
        uint32_t imageIndex = 0;
        RHITexture* color = nullptr; // non-owning
    };

    class RHISwapchain
    {
    public:
        virtual ~RHISwapchain() = default;

        virtual Format colorFormat() const = 0;
        virtual Extent2D extent() const = 0;
        virtual uint32_t imageCount() const = 0;
        virtual uint32_t framesInFlight() const = 0;

        // Contract:
        // - cmd must NOT be in recording state (swapchain will reset/begin it once the frame fence is satisfied).
        // - On success, out.color is a valid backbuffer texture for this frame.
        // - The swapchain will record a transition to ColorAttachment for the acquired image.
        virtual bool beginFrame(uint32_t frameIndex, RHICommandBuffer* cmd, SwapchainFrame& out) = 0;

        // Contract:
        // - cmd must be in recording state and already contains all rendering commands targeting the acquired image.
        // - The swapchain will record a transition to Present, end the command buffer, submit, and present.
        virtual bool endFrame(uint32_t frameIndex, RHICommandBuffer* cmd) = 0;

        // Explicit swapchain rebuild (used on OUT_OF_DATE / resize).
        virtual void recreate(uint32_t width, uint32_t height) = 0;

        virtual void setVsync(bool enabled) = 0;
    };
} // namespace pnkr::renderer::rhi
