#include "pnkr/debug/RenderDoc.hpp"
#include "thirdparty/renderdoc/renderdoc_app.h" // Must be in include path
#include "pnkr/core/logger.hpp"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace pnkr::debug {

bool RenderDoc::init() {
    if (m_api) return true; // Already initialized

    pRENDERDOC_GetAPI getApiFunc = nullptr;
    constexpr RENDERDOC_Version kRenderDocApiVersion = eRENDERDOC_API_Version_1_6_0;

#if defined(_WIN32)
    // 1. Try to get handle if already loaded (e.g. launched via RenderDoc UI)
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        getApiFunc = reinterpret_cast<pRENDERDOC_GetAPI>(
            GetProcAddress(mod, "RENDERDOC_GetAPI"));
    }

    // At init, on linux/android.
    // For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so

#else
    // Linux/Android logic
    if (void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
    {
        getApiFunc = reinterpret_cast<pRENDERDOC_GetAPI>(
            dlsym(mod, "RENDERDOC_GetAPI"));
    }
#endif

    if (!getApiFunc) {
        // Only warn, don't error, as RD might just not be present
        return false;
    }

    int ret = getApiFunc(kRenderDocApiVersion, reinterpret_cast<void**>(&m_api));
    if (ret != 1 || !m_api) {
        pnkr::core::Logger::error("RenderDoc: Failed to initialize API 1.6.0");
        m_api = nullptr;
        return false;
    }

    // Configure RenderDoc
    m_api->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync, 1);
    m_api->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen, 1);
    m_api->SetCaptureOptionU32(eRENDERDOC_Option_CaptureAllCmdLists, 1);
    m_api->SetCaptureOptionU32(eRENDERDOC_Option_SaveAllInitials, 1);
    m_api->SetCaptureKeys(nullptr, 0); // Disable RD built-in keys to use our own

    pnkr::core::Logger::info("RenderDoc: Initialized successfully.");
    return true;
}

void RenderDoc::toggleCapture() {
    if (!m_api) return;

    if (!m_capturing) {
        // Start capture on ALL devices/windows
        m_api->StartFrameCapture(nullptr, nullptr);
        m_capturing = true;
        pnkr::core::Logger::info("RenderDoc: Capture STARTED");
    } else {
        // End capture
        uint32_t result = m_api->EndFrameCapture(nullptr, nullptr);
        m_capturing = false;
        
        if (result == 1) {
            pnkr::core::Logger::info("RenderDoc: Capture SAVED successfully.");
        } else {
            pnkr::core::Logger::warn("RenderDoc: Capture failed or discarded.");
        }
    }
}

void RenderDoc::launchReplayUI() {
    if (m_api) {
        // 1 = Connect to existing instance if possible, "" = empty cmdline
        m_api->LaunchReplayUI(1, "");
    }
}

std::string RenderDoc::getOverlayText() const {
    if (!m_api) return "RenderDoc: Not Loaded";
    if (m_capturing) return "RenderDoc: CAPTURING...";
    if (m_api->IsTargetControlConnected()) return "RenderDoc: Connected";
    return "RenderDoc: Ready";
}

} // namespace pnkr::debug
