#include "rhi/vulkan/AftermathIntegration.hpp"
#include "pnkr/core/logger.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>

namespace pnkr::renderer::rhi::vulkan {

    bool AftermathIntegration::s_initialized = false;

    bool AftermathIntegration::isEnabled() {
        return s_initialized;
    }

#ifdef PNKR_AFTERMATH_ENABLED

    void AftermathIntegration::initialize() {
      if (s_initialized) {
        return;
      }

        GFSDK_Aftermath_Result result = GFSDK_Aftermath_EnableGpuCrashDumps(
            GFSDK_Aftermath_Version_API,
            GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
            GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
            gpuCrashDumpCallback,
            shaderDebugInfoCallback,
            crashDumpDescriptionCallback,
            resolveMarkerCallback,
            nullptr
        );

        if (result != GFSDK_Aftermath_Result_Success) {
            core::Logger::RHI.error("Failed to initialize NVIDIA Aftermath: {}", (int)result);
            return;
        }

        s_initialized = true;
        core::Logger::RHI.info("NVIDIA Aftermath initialized.");
    }

    void AftermathIntegration::shutdown() {
        if (s_initialized) {
            GFSDK_Aftermath_DisableGpuCrashDumps();
            s_initialized = false;
        }
    }

    void AftermathIntegration::gpuCrashDumpCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData) {
        (void)pUserData;

        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::stringstream ss;
        ss << "pnkr_crash_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".nv-gpudmp";
        std::string filename = ss.str();

        std::ofstream dumpFile(filename, std::ios::binary);
        if (dumpFile) {
            dumpFile.write(reinterpret_cast<const char*>(pGpuCrashDump), gpuCrashDumpSize);
            dumpFile.close();
            core::Logger::RHI.critical("NVIDIA Aftermath: GPU Crash Dump saved to '{}'", filename);
        } else {
            core::Logger::RHI.critical("NVIDIA Aftermath: Failed to write GPU Crash Dump to '{}'", filename);
        }
    }

    void AftermathIntegration::shaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData) {
        (void)pUserData;

        static int counter = 0;
        std::string filename = "shader_debug_info_" + std::to_string(counter++) + ".nv-dbg";

        std::ofstream dumpFile(filename, std::ios::binary);
        if (dumpFile) {
            dumpFile.write(reinterpret_cast<const char*>(pShaderDebugInfo), shaderDebugInfoSize);
            dumpFile.close();
        }
    }

    void AftermathIntegration::crashDumpDescriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* pUserData) {
        (void)pUserData;
        addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "PNKR Engine");
        addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "0.1.0");
    }

    void AftermathIntegration::resolveMarkerCallback(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, PFN_GFSDK_Aftermath_ResolveMarker resolveMarker) {

        (void)pMarkerData; (void)markerDataSize; (void)pUserData; (void)resolveMarker;
    }

#else
    void AftermathIntegration::initialize() {
        core::Logger::RHI.info("NVIDIA Aftermath disabled in build.");
    }
    void AftermathIntegration::shutdown() {}
#endif

}

