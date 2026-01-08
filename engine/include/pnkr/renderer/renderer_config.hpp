#pragma once
#include "pnkr/rhi/rhi_device.hpp"

namespace pnkr::renderer {

struct RendererConfig
{
    bool m_enableValidation = true;
    bool m_useBindless = true;
    uint32_t m_maxFramesInFlight = 3;
    bool m_enableBindless = true;
    bool m_enableAsyncTextureLoading = true;
};

}
