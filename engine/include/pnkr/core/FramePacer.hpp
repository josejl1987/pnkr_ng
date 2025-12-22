#pragma once

#include <chrono>
#include <thread>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <timeapi.h>
#endif

namespace pnkr::core {

class FramePacer {
public:
    FramePacer() {
        m_lastFrameTime = std::chrono::high_resolution_clock::now();
    }

    void paceFrame(double targetFPS) {
        if (targetFPS <= 0.0) {
            m_lastFrameTime = std::chrono::high_resolution_clock::now();
            return;
        }

        const auto targetFrameTime = std::chrono::duration<double>(1.0 / targetFPS);
        const auto currentTime = std::chrono::high_resolution_clock::now();
        const auto frameDuration = currentTime - m_lastFrameTime;
        
        auto sleepTime = targetFrameTime - frameDuration;

#ifdef _WIN32
        // Subtract a small epsilon to avoid oversleeping on Windows
        sleepTime -= std::chrono::duration<double>(std::chrono::milliseconds(1));
#endif

        if (sleepTime > std::chrono::duration<double>(0)) {
#ifdef _WIN32
            timeBeginPeriod(1);
#endif
            std::this_thread::sleep_for(sleepTime);
#ifdef _WIN32
            timeEndPeriod(1);
#endif
        }

        m_lastFrameTime = std::chrono::high_resolution_clock::now();
    }

private:
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};

} // namespace pnkr::core
