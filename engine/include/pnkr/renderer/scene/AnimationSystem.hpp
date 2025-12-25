#pragma once
#include "pnkr/renderer/scene/ModelDOD.hpp"

namespace pnkr::renderer::scene
{
    class AnimationSystem
    {
    public:
        static void update(ModelDOD& model, float dt);
        
        // New: Compute joint matrices for skinning
        static std::vector<glm::mat4> updateSkinning(const ModelDOD& model);

    private:
        static void applyAnimation(ModelDOD& model, const Animation& anim, float time);

        // Helper to find keyframe index
        static size_t getKeyframeIndex(const std::vector<float>& inputs, float time);

        // Interpolation helpers
        static glm::vec3 interpolateTranslation(const AnimationSampler& sampler, float time);
        static glm::quat interpolateRotation(const AnimationSampler& sampler, float time);
        static glm::vec3 interpolateScale(const AnimationSampler& sampler, float time);
        static void interpolateWeights(const AnimationSampler& sampler, float time, uint32_t numTargets, float* outWeights);
    };
} // namespace pnkr::renderer::scene
