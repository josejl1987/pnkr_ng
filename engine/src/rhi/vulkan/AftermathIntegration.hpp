#pragma once

#include "pnkr/core/common.hpp"
#include <vector>

#if defined(PNKR_AFTERMATH_ENABLED)
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#endif

namespace pnkr::renderer::rhi::vulkan {

    class AftermathIntegration {
    public:
        static void initialize();
        static void shutdown();

        static bool isEnabled();

    private:
#if defined(PNKR_AFTERMATH_ENABLED)
        static void gpuCrashDumpCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData);
        static void shaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData);
        static void crashDumpDescriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* pUserData);
        static void resolveMarkerCallback(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, PFN_GFSDK_Aftermath_ResolveMarker resolveMarker);
#endif
        static bool s_initialized;
    };

}
