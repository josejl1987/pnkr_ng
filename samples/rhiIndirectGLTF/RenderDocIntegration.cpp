#include "RenderDocIntegration.hpp"

#include <sstream>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

// RenderDoc header: vendored into the repo.
#include "renderdoc_app.h"

namespace pnkr::samples {

static std::string ptrToStr(const void* p)
{
    std::ostringstream ss;
    ss << p;
    return ss.str();
}

bool RenderDocIntegration::loadRenderDocLibrary()
{
#if defined(_WIN32)
    // RenderDoc injects itself when launched from the UI, but for manual load we try common names.
    const char* candidates[] = {
        "renderdoc.dll",
        "renderdoccmd.dll" // sometimes present, but API typically in renderdoc.dll
    };

    for (const char* name : candidates) {
        HMODULE mod = GetModuleHandleA(name);
        if (!mod) mod = LoadLibraryA(name);
        if (!mod) continue;

        m_libHandle = (void*)mod;
        return true;
    }
    return false;
#else
    const char* candidates[] = {
        "librenderdoc.so",
        "librenderdoc.so.1"
    };

    for (const char* name : candidates) {
        void* handle = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
        if (!handle) handle = dlopen(name, RTLD_NOW);
        if (!handle) continue;

        m_libHandle = handle;
        return true;
    }
    return false;
#endif
}

bool RenderDocIntegration::init()
{
    if (m_available) return true;

    if (!loadRenderDocLibrary())
        return false;

#if defined(_WIN32)
    auto getApi = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)m_libHandle, "RENDERDOC_GetAPI");
#else
    auto getApi = (pRENDERDOC_GetAPI)dlsym(m_libHandle, "RENDERDOC_GetAPI");
#endif

    if (!getApi) {
        m_libHandle = nullptr;
        return false;
    }

    int ok = getApi(eRENDERDOC_API_Version_1_6_0, (void**)&m_api);
    if (!ok || !m_api) {
        m_api = nullptr;
        m_libHandle = nullptr;
        return false;
    }

    // Optional: disable capture keys to avoid conflicts with your input
    // m_api->SetCaptureKeys(nullptr, 0);

    m_available = true;
    return true;
}

void RenderDocIntegration::startCapture(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;
    if (m_capturing) return;

    m_api->StartFrameCapture(device, windowHandle);
    m_capturing = true;
}

void RenderDocIntegration::endCapture(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;
    if (!m_capturing) return;

    m_api->EndFrameCapture(device, windowHandle);
    m_capturing = false;
}

void RenderDocIntegration::requestCaptureFrames(uint32_t frames, bool andLaunchUI)
{
    if (frames == 0) return;
    m_captureFramesRemaining = frames;
    m_launchUIOnFinish = andLaunchUI;
}

void RenderDocIntegration::onFrameBegin(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;

    m_shouldEndThisFrame = false;

    // If we are requested to capture frames, start capture at the beginning of the frame.
    if (m_captureFramesRemaining > 0 && !m_capturing) {
        startCapture(device, windowHandle);
        m_shouldEndThisFrame = true;
        // We'll decrement when we successfully end.
    }
}

void RenderDocIntegration::onFrameEnd(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;

    if (m_shouldEndThisFrame) {
        endCapture(device, windowHandle);
        m_shouldEndThisFrame = false;

        if (m_captureFramesRemaining > 0)
            --m_captureFramesRemaining;

        if (m_captureFramesRemaining == 0 && m_launchUIOnFinish) {
            m_launchUIOnFinish = false;
            if (!launchReplayUI(1)) {
                // If it was already open, LaunchReplayUI might return 0,
                // try ShowReplayUI to bring it to front.
                m_api->ShowReplayUI();
            }
        }
    }
}

bool RenderDocIntegration::launchReplayUI(uint32_t connectTargetControlPort)
{
    if (!m_available || !m_api) return false;

    // Try to find the latest capture to open it directly
    const char* latestCapturePath = nullptr;
    uint32_t numCaptures = m_api->GetNumCaptures();
    if (numCaptures > 0) {
        // We need a buffer for the path. GetCapture fills it.
        // For simplicity, we can pass nullptr to cmdline in LaunchReplayUI
        // which will connect to the app and show all captures.
        // However, if we want to BE SURE it opens the latest one:
        char filename[1024];
        uint32_t pathLength = 1024;
        if (m_api->GetCapture(numCaptures - 1, filename, &pathLength, nullptr)) {
            latestCapturePath = filename;
        }
    }

    // Launch the UI. connectTargetControlPort=1 connects back to the app.
    uint32_t pid = m_api->LaunchReplayUI(connectTargetControlPort, latestCapturePath);
    return pid != 0;
}

std::string RenderDocIntegration::statusString() const
{
    if (!m_available) return "RenderDoc: not available";
    std::ostringstream ss;
    ss << "RenderDoc: available"
       << " | capturing=" << (m_capturing ? "true" : "false")
       << " | pendingFrames=" << m_captureFramesRemaining
       << " | api=" << ptrToStr(m_api);
    return ss.str();
}

} // namespace pnkr::samples
