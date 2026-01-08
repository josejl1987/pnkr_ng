#pragma once

#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <timeapi.h>
    #pragma comment(lib, "winmm.lib")
#endif

namespace pnkr::core {

class FramePacer {
public:
    FramePacer() : m_nextFrameTime(std::chrono::steady_clock::now()) {
        acquireTimerPeriod();
    }

    ~FramePacer() {
        releaseTimerPeriod();
    }

    void paceFrame(double targetFPS) {
        if (targetFPS <= 0.0) {
            m_nextFrameTime = std::chrono::steady_clock::now();
            return;
        }

        const auto targetFrameDuration = std::chrono::duration<double>(1.0 / targetFPS);

        m_nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(targetFrameDuration);

        auto now = std::chrono::steady_clock::now();

        if (now > m_nextFrameTime + std::chrono::milliseconds(100)) {
            m_nextFrameTime = now;
            return;
        }

        auto timeToSleep = m_nextFrameTime - now - std::chrono::milliseconds(2);

        if (timeToSleep > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(timeToSleep);
        }

        while (std::chrono::steady_clock::now() < m_nextFrameTime) {
        }
    }

private:
    std::chrono::steady_clock::time_point m_nextFrameTime;

    static void acquireTimerPeriod() {
#if defined(_WIN32)

        timeBeginPeriod(1);
#endif
    }

    static void releaseTimerPeriod() {
#if defined(_WIN32)
        timeEndPeriod(1);
#endif
    }
};

}
