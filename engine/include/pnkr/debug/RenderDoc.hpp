#pragma once

#include <cstdint>
#include <string>

// Forward declaration to avoid including the heavy renderdoc_app.h here
struct RENDERDOC_API_1_6_0;

namespace pnkr::debug {

class RenderDoc {
public:
    RenderDoc() = default;
    ~RenderDoc() = default;

    // Returns true if RenderDoc was found and loaded
    bool init();
    
    // Returns true if the RenderDoc API is loaded and ready
    bool isAvailable() const { return m_api != nullptr; }
    
    // Returns true if a capture is currently in progress
    bool isCapturing() const { return m_capturing; }

    // The main toggle function (Yuzu style)
    // Starts or Ends a capture immediately on all active devices/windows
    void toggleCapture();

    // Utility: Launch the RenderDoc UI tool
    void launchReplayUI();

    // Diagnostics
    std::string getOverlayText() const;

private:
    RENDERDOC_API_1_6_0* m_api = nullptr;
    bool m_capturing = false;
};

} // namespace pnkr::debug