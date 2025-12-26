#pragma once

#include <chrono>
#include <thread>
#include <atomic>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <timeapi.h>
#endif

namespace pnkr::core {

class FramePacer {
public:
    FramePacer() {
        acquireTimerPeriod();
        m_lastFrameTime = std::chrono::steady_clock::now();
    }

    ~FramePacer() {
        releaseTimerPeriod();
    }

    void paceFrame(double targetFPS) {
        if (targetFPS <= 0.0) {
            m_lastFrameTime = std::chrono::steady_clock::now();
            return;
        }

        const auto targetFrameTime = std::chrono::duration<double>(1.0 / targetFPS);
        const auto currentTime = std::chrono::steady_clock::now();
        const auto frameDuration = currentTime - m_lastFrameTime;
        
        auto sleepTime = targetFrameTime - frameDuration;

#ifdef _WIN32
        // Subtract a small epsilon to avoid oversleeping on Windows
        sleepTime -= std::chrono::duration<double>(std::chrono::milliseconds(1));
#endif

        if (sleepTime > std::chrono::duration<double>(0)) {
            std::this_thread::sleep_for(sleepTime);
        }

        m_lastFrameTime = std::chrono::steady_clock::now();
    }

private:
    static void acquireTimerPeriod() {
#if defined(_WIN32)
        if (s_timerUsers.fetch_add(1, std::memory_order_relaxed) == 0) {
            timeBeginPeriod(1);
        }
#endif
    }

    static void releaseTimerPeriod() {
#if defined(_WIN32)
        if (s_timerUsers.fetch_sub(1, std::memory_order_relaxed) == 1) {
            timeEndPeriod(1);
        }
#endif
    }

    std::chrono::steady_clock::time_point m_lastFrameTime;
    static inline std::atomic<uint32_t> s_timerUsers{0};
};

} // namespace pnkr::core
