#pragma once

#include <chrono>

namespace pnkr::core {

    class Timer {
    public:
        Timer() { reset(); }

        void reset() {
            m_lastFrame = std::chrono::steady_clock::now();
        }

        [[nodiscard]] float deltaTime() {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> delta = now - m_lastFrame;
            m_lastFrame = now;
            return delta.count();
        }

        [[nodiscard]] float elapsed() const {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> delta = now - m_lastFrame;
            return delta.count();
        }

    private:
        std::chrono::steady_clock::time_point m_lastFrame;
    };

}
