#pragma once
#include <cstdint>
#include <string>

// Forward-declared RenderDoc API pointer in global namespace.
struct RENDERDOC_API_1_6_0;

namespace pnkr::samples {

class RenderDocIntegration {
public:
    // Attempts to locate and load RenderDoc at runtime.
    // Safe to call multiple times; returns true if API is available.
    bool init();

    bool isAvailable() const { return m_available; }
    bool isCapturing() const { return m_capturing; }

    // Capture control. For Vulkan you can pass nullptr/nullptr and it will capture globally.
    void startCapture(void* device = nullptr, void* windowHandle = nullptr);
    void endCapture(void* device = nullptr, void* windowHandle = nullptr);

    // Convenience: capture N consecutive frames using the frame hooks below.
    void requestCaptureFrames(uint32_t frames, bool andLaunchUI = true);

    // These must be called from your frame loop:
    // - onFrameBegin(): before recording/submitting for the frame
    // - onFrameEnd(): after present (or at least after submitting all work)
    void onFrameBegin(void* device = nullptr, void* windowHandle = nullptr);
    void onFrameEnd(void* device = nullptr, void* windowHandle = nullptr);

    // Optional helper (works when RenderDoc UI path is discoverable)
    // In practice, LaunchReplayUI is supported but may require renderdoc.dll already loaded.
    bool launchReplayUI(uint32_t connectTargetControlPort = 0);

    std::string statusString() const;

private:
    bool loadRenderDocLibrary();

private:
    bool m_available = false;
    bool m_capturing = false;

    // requested capture count (N frames)
    uint32_t m_captureFramesRemaining = 0;

    // internal: whether we started capture at this frame begin and need to end it at frame end
    bool m_shouldEndThisFrame = false;

    // whether to launch UI after finishing current capture request
    bool m_launchUIOnFinish = false;

    // opaque handles
    void* m_libHandle = nullptr;

    // The API pointer.
    ::RENDERDOC_API_1_6_0* m_api = nullptr;
};

} // namespace pnkr::samples
