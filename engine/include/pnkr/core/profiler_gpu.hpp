#pragma once

#include "pnkr/core/profiler.hpp"

// =============================================================================
// Tracy Vulkan GPU Profiling Integration
// =============================================================================
// This header provides GPU timeline profiling macros that work with Tracy.
// Include this header ONLY in files that need Vulkan GPU profiling.
// =============================================================================

#if defined(TRACY_ENABLE)
#include <vulkan/vulkan.h>
#include <tracy/TracyVulkan.hpp>

// Override the stub macros from profiler.hpp with actual implementations
#undef PNKR_PROFILE_GPU_CONTEXT
#undef PNKR_PROFILE_GPU_CONTEXT_CALIBRATED
#undef PNKR_PROFILE_GPU_DESTROY
#undef PNKR_PROFILE_GPU_COLLECT
#undef PNKR_PROFILE_GPU_ZONE
#undef PNKR_RHI_GPU_ZONE

// Create a Tracy Vulkan context for GPU profiling
// Create a Tracy Vulkan context for GPU profiling
#define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuf) \
    ((void*)(tracy::CreateVkContext(physDev, dev, queue, cmdBuf, nullptr, nullptr)))

// Create a calibrated Tracy Vulkan context (for better CPU-GPU sync)
#define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuf, \
                                             getTsFn, getCpuTsFn) \
    ((void*)(tracy::CreateVkContext(physDev, dev, queue, cmdBuf, getTsFn, getCpuTsFn)))

// Destroy a Tracy Vulkan context
#define PNKR_PROFILE_GPU_DESTROY(ctx) \
    do { if ((ctx) != nullptr) tracy::DestroyVkContext((tracy::VkCtx*)(ctx)); } while(0)

// Collect GPU timing data (call at end of frame or after queue submit)
// Note: Only collects if ctx is non-null
#define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuf) \
    do { if ((ctx) != nullptr) TracyVkCollect(((tracy::VkCtx*)(ctx)), cmdBuf); } while(0)

// Create a GPU-side profiling zone (for raw VkCommandBuffer)
// Note: Uses 'active' parameter to become no-op if ctx is null
#define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuf, name) \
    TracyVkNamedZone((tracy::VkCtx*)(ctx), ___tracy_gpu_zone, cmdBuf, name, ((ctx) != nullptr))

// Create a GPU-side profiling zone (for RHI command list)
// Note: Uses 'active' parameter to become no-op if ctx is null
#define PNKR_RHI_GPU_ZONE(ctx, rhiCmd, name) \
    PNKR_PROFILE_GPU_ZONE(ctx, static_cast<VkCommandBuffer>((rhiCmd)->nativeHandle()), name)

#endif // TRACY_ENABLE
