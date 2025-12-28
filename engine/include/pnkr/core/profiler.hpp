#pragma once

#include <cstring>

#if defined(TRACY_ENABLE)
    #include <tracy/Tracy.hpp>
    #include <vulkan/vulkan.hpp>
    #include <tracy/TracyVulkan.hpp>

    // CPU Profiling Macros
    #define PNKR_PROFILE_FRAME(name) FrameMarkNamed(name)
    #define PNKR_PROFILE_FRAME_MARK() FrameMark
    #define PNKR_PROFILE_FUNCTION() ZoneScoped
    #define PNKR_PROFILE_SCOPE(name) ZoneScopedN(name)
    #define PNKR_PROFILE_TAG(str) ZoneText(str, strlen(str))

    // GPU Profiling Types
    using TracyContext = TracyVkCtx;

    // GPU Profiling Macros
    #define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuffer) \
            TracyVkContext(physDev, dev, queue, cmdBuffer)

    #define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuffer, func1, func2) \
            TracyVkContextCalibrated(physDev, dev, queue, cmdBuffer, func1, func2)

    #define PNKR_PROFILE_GPU_DESTROY(ctx) TracyVkDestroy(ctx)

    #define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuffer) TracyVkCollect(ctx, cmdBuffer)

    #define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuffer, name) TracyVkZone(ctx, cmdBuffer, name)

    // Helper for RHI Command Buffers
    #define PNKR_RHI_GPU_ZONE(ctx, rhiCmd, name) \
        PNKR_PROFILE_GPU_ZONE(ctx, static_cast<VkCommandBuffer>((rhiCmd)->nativeHandle()), name)

#else
    // Empty macros when disabled
    #define PNKR_PROFILE_FRAME(name)
    #define PNKR_PROFILE_FRAME_MARK()
    #define PNKR_PROFILE_FUNCTION()
    #define PNKR_PROFILE_SCOPE(name)
    #define PNKR_PROFILE_TAG(str)

    using TracyContext = void*;

    #define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuffer) nullptr
    #define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuffer, func1, func2) nullptr
    #define PNKR_PROFILE_GPU_DESTROY(ctx)
    #define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuffer)
    #define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuffer, name)

#endif
