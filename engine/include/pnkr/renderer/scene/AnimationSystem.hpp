#pragma once
#include "pnkr/renderer/scene/ModelDOD.hpp"

namespace pnkr::renderer::scene
{
    class AnimationSystem
    {
    public:
        static void update(ModelDOD& model, float dt);

        static void updateBlending(ModelDOD& model,
                                   AnimationState& stateA,
                                   AnimationState& stateB,
                                   float blendWeight,
                                   float dt);

        static std::vector<glm::mat4> updateSkinning(const ModelDOD& model);

        static BoundingBox calculateSkinnedBounds(const ModelDOD& model);

    private:
        static void applyAnimation(ModelDOD& model, const Animation& anim, float time);

        static void applyBlending(ModelDOD& model,
                                  const Animation& animA, float timeA,
                                  const Animation& animB, float timeB,
                                  float weight);

        static size_t getKeyframeIndex(const std::vector<float>& inputs, float time);

        struct InterpolationContext {
            size_t idx0;
            size_t idx1;
            float factor;
            float dt;
        };
        static InterpolationContext getInterpolationContext(const std::vector<float>& inputs, float time);

        static glm::vec3 interpolateTranslation(const AnimationSampler& sampler, float time);
        static glm::quat interpolateRotation(const AnimationSampler& sampler, float time);
        static glm::vec3 interpolateScale(const AnimationSampler& sampler, float time);
        static void interpolateWeights(const AnimationSampler& sampler, float time, uint32_t numTargets, float* outWeights);
    };
}
