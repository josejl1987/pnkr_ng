#pragma once

/**
 * @file engine.hpp
 * @brief Main PNKR engine header - include this for all engine functionality
 */

#define PNKR_VERSION_MAJOR 0
#define PNKR_VERSION_MINOR 1
#define PNKR_VERSION_PATCH 0

#include "core/Timer.h"
#include "pnkr/core/logger.hpp"
#include "pnkr/platform/window.hpp"
#include "pnkr/renderer/renderer_config.hpp"

namespace pnkr
{
    using Log = core::Logger;
    using Window = platform::Window;
    using RendererConfig = renderer::RendererConfig;
    using Timer = core::Timer;
    using Input = platform::Input;
}
