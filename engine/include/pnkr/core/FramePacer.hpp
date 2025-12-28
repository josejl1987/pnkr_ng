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

        using namespace std::chrono;

        // Calculate target duration
        const auto targetFrameDuration = duration<double>(1.0 / targetFPS);

        // 1. Advance the schedule by exactly one frame interval
        m_nextFrameTime += duration_cast<steady_clock::duration>(targetFrameDuration);

        auto now = steady_clock::now();

        // 2. Drift Recovery: If we are WAY behind (e.g. > 100ms), snap the schedule to now.
        // This prevents the "spiral of death" where we try to render 50 frames instantly to catch up.
        if (now > m_nextFrameTime + milliseconds(100)) {
            m_nextFrameTime = now;
            return; // Don't sleep, just get back to work
        }

        // 3. Hybrid Wait Strategy
        // We want to sleep to save power, but sleep is imprecise.
        // We sleep until 1.5ms BEFORE the target, then spin-wait the rest.
        auto timeToSleep = m_nextFrameTime - now - milliseconds(2); // 1.5ms + safety margin

        if (timeToSleep > milliseconds(0)) {
            std::this_thread::sleep_for(timeToSleep);
        }

        // 4. Spin Lock for precision (Burn remaining time)
        while (steady_clock::now() < m_nextFrameTime) {
            // std::this_thread::yield(); // Optional: reduces CPU slightly, but reduces precision
            // _mm_pause(); // Ideally use an intrinsic for "cpu relax" if available
        }
    }

private:
    std::chrono::steady_clock::time_point m_nextFrameTime;

    static void acquireTimerPeriod() {
#if defined(_WIN32)
        // Set global timer resolution to 1ms to make sleep_for() more accurate
        timeBeginPeriod(1);
#endif
    }

    static void releaseTimerPeriod() {
#if defined(_WIN32)
        timeEndPeriod(1);
#endif
    }
};

} // namespace pnkr::core
