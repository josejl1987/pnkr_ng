#pragma once

#include <cstring>
#include <mutex>

#if defined(TRACY_ENABLE)
    #include <tracy/Tracy.hpp>
#endif

#if defined(PNKR_ENABLE_VTUNE)
    #include <ittnotify.h>

    namespace pnkr::core::profiler {
        struct VTuneDomain {
            static __itt_domain* Get() {
                static __itt_domain* domain = __itt_domain_create("PNKR");
                return domain;
            }
        };

        struct ScopedVTuneTask {
            ScopedVTuneTask(const char* name) {
                // Ideally this should use a static handle for performance, but for a generic wrapper 
                // we'd need a macro that declares a static local handle.
                // For now, creating the handle on the fly is acceptable for coarse profiling.
                static __itt_domain* domain = VTuneDomain::Get();
                __itt_string_handle* handle = __itt_string_handle_create(name);
                __itt_task_begin(domain, __itt_null, __itt_null, handle);
            }
            
            ~ScopedVTuneTask() {
                static __itt_domain* domain = VTuneDomain::Get();
                __itt_task_end(domain);
            }
        };
    }
    
    #define PNKR_VTUNE_SCOPE_IMPL(name) pnkr::core::profiler::ScopedVTuneTask vtune_scope_##__LINE__(name)
    #define PNKR_VTUNE_FRAME_BEGIN() __itt_frame_begin_v3(pnkr::core::profiler::VTuneDomain::Get(), NULL)
    #define PNKR_VTUNE_FRAME_END()   __itt_frame_end_v3(pnkr::core::profiler::VTuneDomain::Get(), NULL)
#else
    #define PNKR_VTUNE_SCOPE_IMPL(name)
    #define PNKR_VTUNE_FRAME_BEGIN()
    #define PNKR_VTUNE_FRAME_END()
#endif


#if defined(TRACY_ENABLE)

    #define PNKR_PROFILE_FRAME(name) FrameMarkNamed(name); PNKR_VTUNE_FRAME_END()
    #define PNKR_PROFILE_FRAME_MARK() FrameMark; PNKR_VTUNE_FRAME_END()
    #define PNKR_PROFILE_FRAME_BEGIN() PNKR_VTUNE_FRAME_BEGIN()
    #define PNKR_PROFILE_FRAME_END()   PNKR_VTUNE_FRAME_END()
    
    // Combine Tracy and VTune scopes
    #define PNKR_PROFILE_FUNCTION() ZoneScoped; PNKR_VTUNE_SCOPE_IMPL(__func__)
    #define PNKR_PROFILE_SCOPE(name) ZoneScopedN(name); PNKR_VTUNE_SCOPE_IMPL(name)
    #define PNKR_PROFILE_SCOPE_COLOR(name, color) ZoneScopedNC(name, color); PNKR_VTUNE_SCOPE_IMPL(name)
    
    #define PNKR_PROFILE_TAG(str) ZoneText(str, strlen(str))

    #define PNKR_TRACY_PLOT(name, value) TracyPlot(name, value)
    #define PNKR_TRACY_MESSAGE(msg, size) TracyMessage(msg, size)

    using TracyContext = void*;

    #define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuffer) nullptr
    #define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuffer, func1, func2) nullptr
    #define PNKR_PROFILE_GPU_DESTROY(ctx)
    #define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuffer)

    #define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuffer, name)
    #define PNKR_RHI_GPU_ZONE(ctx, rhiCmd, name)

    using PNKR_MUTEX = tracy::Lockable<std::mutex>;
    #define PNKR_MUTEX_DECL(name, desc) TracyLockableN(std::mutex, name, desc)

#else

    #define PNKR_PROFILE_FRAME(name) PNKR_VTUNE_FRAME_END()
    #define PNKR_PROFILE_FRAME_MARK() PNKR_VTUNE_FRAME_END()
    #define PNKR_PROFILE_FRAME_BEGIN() PNKR_VTUNE_FRAME_BEGIN()
    #define PNKR_PROFILE_FRAME_END()   PNKR_VTUNE_FRAME_END()
    
    // Only VTune if Tracy is disabled
    #define PNKR_PROFILE_FUNCTION() PNKR_VTUNE_SCOPE_IMPL(__func__)
    #define PNKR_PROFILE_SCOPE(name) PNKR_VTUNE_SCOPE_IMPL(name)
    #define PNKR_PROFILE_SCOPE_COLOR(name, color) PNKR_VTUNE_SCOPE_IMPL(name)
    
    #define PNKR_PROFILE_TAG(str)

    #define PNKR_TRACY_PLOT(name, value) ((void)0)
    #define PNKR_TRACY_MESSAGE(msg, size) ((void)0)

    using TracyContext = void*;

    #define PNKR_PROFILE_GPU_CONTEXT(physDev, dev, queue, cmdBuffer) nullptr
    #define PNKR_PROFILE_GPU_CONTEXT_CALIBRATED(physDev, dev, queue, cmdBuffer, func1, func2) nullptr
    #define PNKR_PROFILE_GPU_DESTROY(ctx)
    #define PNKR_PROFILE_GPU_COLLECT(ctx, cmdBuffer)
    #define PNKR_PROFILE_GPU_ZONE(ctx, cmdBuffer, name)
    #define PNKR_RHI_GPU_ZONE(ctx, rhiCmd, name)

    using PNKR_MUTEX = std::mutex;
    #define PNKR_MUTEX_DECL(name, desc) std::mutex name

#endif
