#pragma once

#include "pnkr/core/profiler.hpp"

#if defined(TRACY_ENABLE)
#include <vulkan/vulkan.h>
#include <tracy/TracyVulkan.hpp>

#undef PNKR_PROFILE_GPU_CONTEXT
#undef PNKR_PROFILE_GPU_CONTEXT_CALIBRATED
#undef PNKR_PROFILE_GPU_DESTROY
#undef PNKR_PROFILE_GPU_COLLECT
#undef PNKR_PROFILE_GPU_ZONE
#undef PNKR_RHI_GPU_ZONE

#define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuffer) \
    static_cast<TracyContext>(tracy::CreateVkContext(physDev, dev, queue, cmdBuffer, nullptr, nullptr))

#define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuffer, func1, func2) \
    static_cast<TracyContext>(tracy::CreateVkContext(physDev, dev, queue, cmdBuffer, func1, func2))

#define PNKR_PROFILE_GPU_DESTROY(ctx) TracyVkDestroy(static_cast<TracyVkCtx>(ctx))

#define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuffer) TracyVkCollect(static_cast<TracyVkCtx>(ctx), cmdBuffer)

#define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuffer, name) TracyVkZone(static_cast<TracyVkCtx>(ctx), cmdBuffer, name)

#define PNKR_RHI_GPU_ZONE(ctx, rhiCmd, name) \
    PNKR_PROFILE_GPU_ZONE(static_cast<TracyVkCtx>(ctx), static_cast<VkCommandBuffer>((rhiCmd)->nativeHandle()), name)

#endif
