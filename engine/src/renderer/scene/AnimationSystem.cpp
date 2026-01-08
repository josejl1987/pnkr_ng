#include "pnkr/renderer/scene/AnimationSystem.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/core/profiler.hpp"
#include <algorithm>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_map>
#include <set>
#include <limits>

namespace pnkr::renderer::scene
{
    void AnimationSystem::update(ModelDOD& model, float dt)
    {
        PNKR_PROFILE_FUNCTION();
        auto& state = model.animationState();

        if (state.isBlending) {
            AnimationState stateA;
            stateA.animIndex = state.animIndex;
            stateA.currentTime = state.currentTime;
            stateA.isLooping = state.isLooping;
            stateA.isPlaying = state.isPlaying;

            AnimationState stateB;
            stateB.animIndex = state.animIndexB;
            stateB.currentTime = state.currentTimeB;
            stateB.isLooping = state.isLooping;
            stateB.isPlaying = true;

            updateBlending(model, stateA, stateB, state.blendWeight, dt);

            state.currentTime = stateA.currentTime;
            state.currentTimeB = stateB.currentTime;
            state.isPlaying = stateA.isPlaying;
            return;
        }

        if (!state.isPlaying || state.animIndex >= model.animations().size())
        {
            return;
        }

        const auto& anim = model.animations()[state.animIndex];

        state.currentTime += dt;
        if (state.currentTime > anim.duration)
        {
            if (state.isLooping)
            {
                state.currentTime = std::fmod(state.currentTime, anim.duration);
            }
            else
            {
                state.currentTime = anim.duration;
                state.isPlaying = false;
            }
        }

        applyAnimation(model, anim, state.currentTime);

        for (const auto& ch : anim.channels)
        {
            model.scene().markAsChanged(ch.targetNode);
        }

        model.scene().setHierarchyDirty(true);
    }

    BoundingBox AnimationSystem::calculateSkinnedBounds(const ModelDOD& model)
    {
      if (model.skins().empty()) {
        return {};
      }

        const auto& skin = model.skins()[0];
        const auto& scene = model.scene();

        BoundingBox bounds{};
        bounds.m_min = glm::vec3(std::numeric_limits<float>::max());
        bounds.m_max = glm::vec3(std::numeric_limits<float>::lowest());

        bool found = false;
        for (ecs::Entity joint : skin.joints) {
            if (scene.registry().has<WorldTransform>(joint)) {
              auto pos = glm::vec3(
                  scene.registry().get<WorldTransform>(joint).matrix[3]);
              bounds.m_min = glm::min(bounds.m_min, pos);
              bounds.m_max = glm::max(bounds.m_max, pos);
              found = true;
            }
        }

        if (!found) {
          return {};
        }

        float padding = 1.0F;
        bounds.m_min -= glm::vec3(padding);
        bounds.m_max += glm::vec3(padding);

        return bounds;
    }

    void AnimationSystem::updateBlending(ModelDOD& model, AnimationState& stateA, AnimationState& stateB, float blendWeight, float dt)
    {
        const uint32_t animCount = util::u32(model.animations().size());
        const bool hasA = stateA.isPlaying && stateA.animIndex < animCount;
        const bool hasB = stateB.isPlaying && stateB.animIndex < animCount;

        if (!hasA && !hasB) {
          return;
        }

        auto advance = [&](AnimationState& state, const Animation& anim) {
            state.currentTime += dt;
            if (state.currentTime > anim.duration) {
              if (state.isLooping) {
                state.currentTime = std::fmod(state.currentTime, anim.duration);
              } else {
                state.currentTime = anim.duration;
                state.isPlaying = false;
              }
            }
        };

        if (hasA && hasB) {
            const auto& animA = model.animations()[stateA.animIndex];
            const auto& animB = model.animations()[stateB.animIndex];
            advance(stateA, animA);
            advance(stateB, animB);
            applyBlending(model, animA, stateA.currentTime, animB, stateB.currentTime, blendWeight);
        } else if (hasA) {
            const auto& animA = model.animations()[stateA.animIndex];
            advance(stateA, animA);
            applyAnimation(model, animA, stateA.currentTime);
            for (const auto &ch : animA.channels) {
              model.scene().markAsChanged(ch.targetNode);
            }
        } else if (hasB) {
            const auto& animB = model.animations()[stateB.animIndex];
            advance(stateB, animB);
            applyAnimation(model, animB, stateB.currentTime);
            for (const auto &ch : animB.channels) {
              model.scene().markAsChanged(ch.targetNode);
            }
        }

        model.scene().setHierarchyDirty(true);
    }

    void AnimationSystem::applyBlending(ModelDOD& model, const Animation& animA, float timeA, const Animation& animB, float timeB, float weight)
    {
        auto& scene = model.scene();

        struct NodeTRS {
          glm::vec3 m_t{};
          bool m_hasT = false;
          glm::quat m_r{};
          bool m_hasR = false;
          glm::vec3 m_s{};
          bool m_hasS = false;
        };

        std::unordered_map<uint32_t, NodeTRS> sampledA;
        std::unordered_map<uint32_t, NodeTRS> sampledB;
        std::set<uint32_t> affectedNodes;

        for (const auto& ch : animA.channels) {
          if (ch.path == AnimationPath::Weights) {
            continue;
          }
            auto& trs = sampledA[ch.targetNode];
            const auto& sampler = animA.samplers[ch.samplerIndex];
            if (ch.path == AnimationPath::Translation) {
              trs.m_t = interpolateTranslation(sampler, timeA);
              trs.m_hasT = true;
            } else if (ch.path == AnimationPath::Rotation) {
              trs.m_r = interpolateRotation(sampler, timeA);
              trs.m_hasR = true;
            } else if (ch.path == AnimationPath::Scale) {
              trs.m_s = interpolateScale(sampler, timeA);
              trs.m_hasS = true;
            }
            affectedNodes.insert(ch.targetNode);
        }

        for (const auto& ch : animB.channels) {
          if (ch.path == AnimationPath::Weights) {
            continue;
          }
            auto& trs = sampledB[ch.targetNode];
            const auto& sampler = animB.samplers[ch.samplerIndex];
            if (ch.path == AnimationPath::Translation) {
              trs.m_t = interpolateTranslation(sampler, timeB);
              trs.m_hasT = true;
            } else if (ch.path == AnimationPath::Rotation) {
              trs.m_r = interpolateRotation(sampler, timeB);
              trs.m_hasR = true;
            } else if (ch.path == AnimationPath::Scale) {
              trs.m_s = interpolateScale(sampler, timeB);
              trs.m_hasS = true;
            }
            affectedNodes.insert(ch.targetNode);
        }

        for (uint32_t nodeIndex : affectedNodes) {
            ecs::Entity entity = nodeIndex;
            if (!scene.registry().has<LocalTransform>(entity)) {
              continue;
            }
            glm::mat4& localMat = scene.registry().get<LocalTransform>(entity).matrix;

            glm::vec3 t; glm::quat r; glm::vec3 s;
            glm::vec3 skew; glm::vec4 perspective;
            glm::decompose(localMat, s, r, t, skew, perspective);

            auto itA = sampledA.find(nodeIndex);
            auto itB = sampledB.find(nodeIndex);

            glm::vec3 tA = (itA != sampledA.end() && itA->second.m_hasT)
                               ? itA->second.m_t
                               : t;
            glm::vec3 tB = (itB != sampledB.end() && itB->second.m_hasT)
                               ? itB->second.m_t
                               : t;
            t = glm::mix(tA, tB, weight);

            glm::quat rA = (itA != sampledA.end() && itA->second.m_hasR)
                               ? itA->second.m_r
                               : r;
            glm::quat rB = (itB != sampledB.end() && itB->second.m_hasR)
                               ? itB->second.m_r
                               : r;
            r = glm::normalize(glm::slerp(rA, rB, weight));

            glm::vec3 sA = (itA != sampledA.end() && itA->second.m_hasS)
                               ? itA->second.m_s
                               : s;
            glm::vec3 sB = (itB != sampledB.end() && itB->second.m_hasS)
                               ? itB->second.m_s
                               : s;
            s = glm::mix(sA, sB, weight);

            localMat = glm::translate(glm::mat4(1.0F), t) * glm::toMat4(r) *
                       glm::scale(glm::mat4(1.0F), s);
            scene.markAsChanged(entity);
        }
    }

    std::vector<glm::mat4> AnimationSystem::updateSkinning(const ModelDOD& model)
    {
        std::vector<glm::mat4> jointMatrices;
        if (model.skins().empty()) {
          return jointMatrices;
        }

        const auto& skin = model.skins()[0];
        jointMatrices.resize(skin.joints.size());

        const auto& scene = model.scene();

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            ecs::Entity entity = skin.joints[i];

            if (scene.registry().has<WorldTransform>(entity)) {
                jointMatrices[i] = scene.registry().get<WorldTransform>(entity).matrix * skin.inverseBindMatrices[i];
            } else {
              jointMatrices[i] = glm::mat4(1.0F);
            }
        }
        return jointMatrices;
    }

    size_t AnimationSystem::getKeyframeIndex(const std::vector<float>& inputs, float time)
    {

      auto it = std::ranges::upper_bound(inputs, time);
      if (it == inputs.begin()) {
        return 0;
      }
        return (size_t)std::distance(inputs.begin(), it) - 1;
    }

    AnimationSystem::InterpolationContext AnimationSystem::getInterpolationContext(const std::vector<float>& inputs, float time)
    {
        size_t idx0 = getKeyframeIndex(inputs, time);
        size_t idx1 = std::min(idx0 + 1, inputs.size() - 1);
        float t0 = inputs[idx0];
        float t1 = inputs[idx1];
        float dt = t1 - t0;
        float factor = (dt != 0.0F) ? (time - t0) / dt : 0.0F;
        return {.idx0 = idx0, .idx1 = idx1, .factor = factor, .dt = dt};
    }

    template <typename T>
    static T cubicSpline(float t, T p0, T m0, T p1, T m1)
    {
        float t2 = t * t;
        float t3 = t2 * t;
        return ((2.0F * t3 - 3.0F * t2 + 1.0F) * p0) +
               ((t3 - 2.0F * t2 + t) * m0) + ((-2.0F * t3 + 3.0F * t2) * p1) +
               ((t3 - t2) * m1);
    }

    glm::vec3 AnimationSystem::interpolateTranslation(const AnimationSampler& sampler, float time)
    {
      if (sampler.inputs.empty()) {
        return glm::vec3(0.0F);
      }
      if (sampler.inputs.size() == 1) {
        return glm::vec3(sampler.outputs[0]);
      }

        auto ctx = getInterpolationContext(sampler.inputs, time);

        if (sampler.interpolation == InterpolationType::Step)
        {
            return glm::vec3(sampler.outputs[ctx.idx0]);
        }

        if (sampler.interpolation == InterpolationType::CubicSpline)
        {

            size_t stride = 3;
            size_t i0 = ctx.idx0 * stride;
            size_t i1 = ctx.idx1 * stride;

            auto p0 = glm::vec3(sampler.outputs[i0 + 1]);
            glm::vec3 m0 = glm::vec3(sampler.outputs[i0 + 2]) * ctx.dt;
            auto p1 = glm::vec3(sampler.outputs[i1 + 1]);
            glm::vec3 m1 = glm::vec3(sampler.outputs[i1 + 0]) * ctx.dt;

            return cubicSpline(ctx.factor, p0, m0, p1, m1);
        }

        return glm::mix(glm::vec3(sampler.outputs[ctx.idx0]), glm::vec3(sampler.outputs[ctx.idx1]), ctx.factor);
    }

    glm::quat AnimationSystem::interpolateRotation(const AnimationSampler& sampler, float time)
    {
        PNKR_PROFILE_FUNCTION();
        if (sampler.inputs.empty()) {
          return {1, 0, 0, 0};
        }

        auto ctx = getInterpolationContext(sampler.inputs, time);

        if (sampler.interpolation != InterpolationType::CubicSpline)
        {
            if (sampler.inputs.size() == 1)
            {
              const auto &v = sampler.outputs[0];
              return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
            }

            const auto &v0 = sampler.outputs[ctx.idx0];
            glm::quat q0(v0.w, v0.x, v0.y, v0.z);

            if (sampler.interpolation == InterpolationType::Step)
            {
                return glm::normalize(q0);
            }

            const auto &v1 = sampler.outputs[ctx.idx1];
            glm::quat q1(v1.w, v1.x, v1.y, v1.z);
            return glm::normalize(glm::slerp(q0, q1, ctx.factor));
        }

        size_t stride = 3;
        size_t i0 = ctx.idx0 * stride;
        size_t i1 = ctx.idx1 * stride;

        auto vP0 = sampler.outputs[i0 + 1];
        auto vM0 = sampler.outputs[i0 + 2];
        auto vP1 = sampler.outputs[i1 + 1];
        auto vM1 = sampler.outputs[i1 + 0];

        glm::quat p0(vP0.w, vP0.x, vP0.y, vP0.z);
        glm::quat m0(vM0.w, vM0.x, vM0.y, vM0.z);
        glm::quat p1(vP1.w, vP1.x, vP1.y, vP1.z);
        glm::quat m1(vM1.w, vM1.x, vM1.y, vM1.z);

        m0 *= ctx.dt;
        m1 *= ctx.dt;

        glm::quat result;
        result.x = cubicSpline(ctx.factor, p0.x, m0.x, p1.x, m1.x);
        result.y = cubicSpline(ctx.factor, p0.y, m0.y, p1.y, m1.y);
        result.z = cubicSpline(ctx.factor, p0.z, m0.z, p1.z, m1.z);
        result.w = cubicSpline(ctx.factor, p0.w, m0.w, p1.w, m1.w);

        return glm::normalize(result);
    }

    glm::vec3 AnimationSystem::interpolateScale(const AnimationSampler& sampler, float time)
    {
        return interpolateTranslation(sampler, time);
    }

    void AnimationSystem::interpolateWeights(const AnimationSampler& sampler, float time, uint32_t numTargets,
                                             float* outWeights)
    {
      if (sampler.inputs.empty() || numTargets == 0) {
        return;
      }

        auto ctx = getInterpolationContext(sampler.inputs, time);

        for (uint32_t i = 0; i < numTargets; ++i)
        {
            if (sampler.interpolation == InterpolationType::Step)
            {
              outWeights[i] = sampler.outputs[(ctx.idx0 * numTargets) + i].x;
            }
            else if (sampler.interpolation == InterpolationType::CubicSpline)
            {
              float p0 =
                  sampler.outputs[((ctx.idx0 * 3 + 1) * numTargets) + i].x;
              float m0 =
                  sampler.outputs[((ctx.idx0 * 3 + 2) * numTargets) + i].x *
                  ctx.dt;
              float p1 =
                  sampler.outputs[((ctx.idx1 * 3 + 1) * numTargets) + i].x;
              float m1 =
                  sampler.outputs[((ctx.idx1 * 3 + 0) * numTargets) + i].x *
                  ctx.dt;
              outWeights[i] = cubicSpline(ctx.factor, p0, m0, p1, m1);
            }
            else
            {
              float v0 = sampler.outputs[(ctx.idx0 * numTargets) + i].x;
              float v1 = sampler.outputs[(ctx.idx1 * numTargets) + i].x;
              outWeights[i] = glm::mix(v0, v1, ctx.factor);
            }
        }
    }

    void AnimationSystem::applyAnimation(ModelDOD& model, const Animation& anim, float time)
    {
        auto& scene = model.scene();

        for (const auto& ch : anim.channels)
        {
            const auto& sampler = anim.samplers[ch.samplerIndex];
            ecs::Entity entity = ch.targetNode;

            if (ch.path == AnimationPath::Weights)
            {
                if (scene.registry().has<MeshRenderer>(entity)) {
                    int32_t meshIdx = scene.registry().get<MeshRenderer>(entity).meshID;
                    if (meshIdx >= 0 && (size_t)meshIdx < model.morphTargetInfos().size())
                    {
                        const auto& info = model.morphTargetInfos()[meshIdx];
                        gpu::MorphState& state = model.morphStates()[meshIdx];
                        state.meshIndex = (uint32_t)meshIdx;

                        auto numTargets = (uint32_t)info.targetOffsets.size();
                        std::vector<float> weights(numTargets);
                        interpolateWeights(sampler, time, numTargets, weights.data());

                        for (uint32_t i = 0; i < std::min(8U, numTargets);
                             ++i) {
                          state.activeTargets[i] = info.targetOffsets[i];
                          state.weights[i] = weights[i];
                        }
                    }
                }
                continue;
            }

            if (!scene.registry().has<LocalTransform>(entity)) {
              continue;
            }
            glm::mat4& localMat = scene.registry().get<LocalTransform>(entity).matrix;

            glm::vec3 translation;
            glm::quat rotation;
            glm::vec3 scale;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(localMat, scale, rotation, translation, skew, perspective);

            if (ch.path == AnimationPath::Translation)
            {
                translation = interpolateTranslation(sampler, time);
            }
            else if (ch.path == AnimationPath::Rotation)
            {
                rotation = interpolateRotation(sampler, time);
            }
            else if (ch.path == AnimationPath::Scale)
            {
                scale = interpolateScale(sampler, time);
            }

            localMat = glm::translate(glm::mat4(1.0F), translation) *
                       glm::toMat4(rotation) *
                       glm::scale(glm::mat4(1.0F), scale);

            scene.markAsChanged(entity);
        }
    }
}
