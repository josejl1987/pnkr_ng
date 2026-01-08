#pragma once

#include <cstdint>
#include <vector>
#include "pnkr/renderer/gpu_shared/SkinningShared.h"

namespace pnkr::renderer::scene
{
    struct AnimationState
    {
        uint32_t animIndex = ~0u;
        float currentTime = 0.0f;
        bool isLooping = true;
        bool isPlaying = false;
        float weight = 1.0f;

        bool isBlending = false;
        uint32_t animIndexB = ~0u;
        float currentTimeB = 0.0f;
        float blendWeight = 0.0f;
    };

    class SceneState
    {
    public:
        SceneState() = default;
        ~SceneState() = default;

        AnimationState& animationState() { return m_animState; }
        const AnimationState& animationState() const { return m_animState; }

        std::vector<gpu::MorphState>& morphStates() { return m_morphStates; }
        const std::vector<gpu::MorphState>& morphStates() const { return m_morphStates; }

        // GPU Buffers for state
        BufferPtr morphVertexBuffer;
        BufferPtr morphStateBuffer;

    private:
        AnimationState m_animState;
        std::vector<gpu::MorphState> m_morphStates;
    };
}
