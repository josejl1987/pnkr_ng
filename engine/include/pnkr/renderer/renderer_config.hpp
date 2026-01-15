#pragma once
#include "pnkr/rhi/rhi_device.hpp"

namespace pnkr::renderer {

struct RendererConfig
{
    static constexpr uint32_t kFramesInFlight = 3;

    bool m_enableValidation = true;
    bool m_useBindless = true;
    uint32_t m_maxFramesInFlight = kFramesInFlight;
    bool m_enableBindless = true;
    bool m_enableAsyncTextureLoading = true;
    rhi::RHIBackend m_backend = rhi::RHIBackend::Vulkan;
    rhi::Format m_swapchainFormat = rhi::Format::B8G8R8A8_SRGB;
};

}
