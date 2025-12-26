#include "pnkr/debug/RenderDoc.hpp"

#include <sstream>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

// RenderDoc header: vendored into the repo.
#include "renderdoc_app.h"

namespace pnkr::debug {

static std::string ptrToStr(const void* p)
{
    std::ostringstream ss;
    ss << p;
    return ss.str();
}

bool RenderDoc::loadRenderDocLibrary()
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

bool RenderDoc::init()
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

    // Optional: set a predictable capture path template
    m_api->SetCaptureFilePathTemplate("pnkr_captures/capture");

    m_available = true;
    return true;
}

bool RenderDoc::tryOpenLatestCapture(uint32_t connectTargetControlPort)
{
    if (!m_available || !m_api) return false;

    const uint32_t numCaptures = m_api->GetNumCaptures();
    if (numCaptures == 0)
        return false;

    // Two-step to get required path length.
    uint32_t pathLength = 0;
    if (!m_api->GetCapture(numCaptures - 1, nullptr, &pathLength, nullptr))
        return false;
    if (pathLength == 0)
        return false;

    std::string filename;
    filename.resize(pathLength);
    if (!m_api->GetCapture(numCaptures - 1, filename.data(), &pathLength, nullptr))
        return false;

    // Ensure NUL termination if returned length includes it.
    if (!filename.empty() && filename.back() == '\0')
        filename.pop_back();

    if (filename.empty())
        return false;

    // Quote the path (Windows paths with spaces).
    std::string cmdline = "\"";
    cmdline += filename;
    cmdline += "\"";

    const uint32_t pid = m_api->LaunchReplayUI(connectTargetControlPort, cmdline.c_str());
    if (pid == 0)
    {
        // If UI is already open, LaunchReplayUI can fail; try bringing it to front.
        m_api->ShowReplayUI();
        return false;
    }
    return true;
}

void RenderDoc::startCapture(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;
    if (m_capturing) return;

    m_api->StartFrameCapture(device, windowHandle);
    m_capturing = true;
}

void RenderDoc::endCapture(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;
    if (!m_capturing) return;

    m_api->EndFrameCapture(device, windowHandle);
    m_capturing = false;
}

void RenderDoc::requestCaptureFrames(uint32_t frames, bool andLaunchUI)
{
    if (frames == 0) return;
    m_captureFramesRemaining = frames;
    m_launchUIOnFinish = andLaunchUI;
}

void RenderDoc::onFrameBegin(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;

    // If we finished a capture last frame and want the UI to open it, keep trying until it exists.
    if (m_openLatestPending)
    {
        // connectTargetControlPort=1 so UI connects back to the running app
        if (tryOpenLatestCapture(1))
            m_openLatestPending = false;
    }

    m_shouldEndThisFrame = false;

    // If we are requested to capture frames, start capture at the beginning of the frame.
    if (m_captureFramesRemaining > 0 && !m_capturing) {
        startCapture(device, windowHandle);
        m_shouldEndThisFrame = true;
        // We'll decrement when we successfully end.
    }
}

void RenderDoc::onFrameEnd(void* device, void* windowHandle)
{
    if (!m_available || !m_api) return;

    if (m_shouldEndThisFrame) {
        endCapture(device, windowHandle);
        m_shouldEndThisFrame = false;

        if (m_captureFramesRemaining > 0)
            --m_captureFramesRemaining;

        if (m_captureFramesRemaining == 0 && m_launchUIOnFinish) {
            m_launchUIOnFinish = false;
            // Defer opening until capture is visible via GetNumCaptures/GetCapture.
            // Otherwise the UI may launch "empty".
            m_openLatestPending = true;
        }
    }
}

bool RenderDoc::launchReplayUI(uint32_t connectTargetControlPort)
{
    if (!m_available || !m_api) return false;

    // If a capture exists, open it; otherwise just launch UI connected to the app.
    if (tryOpenLatestCapture(connectTargetControlPort))
        return true;

    const uint32_t pid = m_api->LaunchReplayUI(connectTargetControlPort, nullptr);
    if (pid == 0)
        m_api->ShowReplayUI();
    return pid != 0;
}

std::string RenderDoc::statusString() const
{
    if (!m_available) return "RenderDoc: not available";
    std::ostringstream ss;
    ss << "RenderDoc: available"
       << " | capturing=" << (m_capturing ? "true" : "false")
       << " | pendingFrames=" << m_captureFramesRemaining
       << " | api=" << ptrToStr(m_api);
    return ss.str();
}

} // namespace pnkr::debug
