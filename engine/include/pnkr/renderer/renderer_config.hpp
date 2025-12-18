#pragma once
#include "pnkr/rhi/rhi_device.hpp"

namespace pnkr::renderer {

struct RendererConfig
{
    bool m_enableValidation = false;
    bool m_useBindless = false;
    uint32_t m_maxFramesInFlight = 2;
    bool m_enableBindless = true;
};

}
