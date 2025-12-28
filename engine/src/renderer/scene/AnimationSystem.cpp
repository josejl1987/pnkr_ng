#include "pnkr/renderer/scene/AnimationSystem.hpp"
#include "pnkr/core/profiler.hpp"
#include <algorithm>
#include <glm/gtx/compatibility.hpp> // for lerp
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_map>
#include <set>

namespace pnkr::renderer::scene
{
    void AnimationSystem::update(ModelDOD& model, float dt)
    {
        PNKR_PROFILE_FUNCTION();
        auto& state = model.animationState();
        if (!state.isPlaying || state.animIndex >= model.animations().size())
        {
            return;
        }

        const auto& anim = model.animations()[state.animIndex];

        // 1. Advance Time
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

        // 2. Apply sampled values to SceneGraph local transforms
        applyAnimation(model, anim, state.currentTime);

        // 3. Mark hierarchy dirty so global transforms get recomputed
        for (const auto& ch : anim.channels)
        {
            model.scene().markAsChanged(static_cast<ecs::Entity>(ch.targetNode));
        }
        
        // Signal the scene that the hierarchy (transforms) has changed
        model.scene().hierarchyDirty = true;
    }

    void AnimationSystem::updateBlending(ModelDOD& model, AnimationState& stateA, AnimationState& stateB, float blendWeight, float dt)
    {
        const uint32_t animCount = static_cast<uint32_t>(model.animations().size());
        const bool hasA = stateA.isPlaying && stateA.animIndex < animCount;
        const bool hasB = stateB.isPlaying && stateB.animIndex < animCount;

        if (!hasA && !hasB) return;

        auto advance = [&](AnimationState& state, const Animation& anim) {
            state.currentTime += dt;
            if (state.currentTime > anim.duration) {
                if (state.isLooping) state.currentTime = std::fmod(state.currentTime, anim.duration);
                else { state.currentTime = anim.duration; state.isPlaying = false; }
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
            for (const auto& ch : animA.channels) model.scene().markAsChanged(static_cast<ecs::Entity>(ch.targetNode));
        } else if (hasB) {
            const auto& animB = model.animations()[stateB.animIndex];
            advance(stateB, animB);
            applyAnimation(model, animB, stateB.currentTime);
            for (const auto& ch : animB.channels) model.scene().markAsChanged(static_cast<ecs::Entity>(ch.targetNode));
        }

        model.scene().hierarchyDirty = true;
    }

    void AnimationSystem::applyBlending(ModelDOD& model, const Animation& animA, float timeA, const Animation& animB, float timeB, float weight)
    {
        auto& scene = model.scene();

        struct NodeTRS {
            glm::vec3 t; bool hasT = false;
            glm::quat r; bool hasR = false;
            glm::vec3 s; bool hasS = false;
        };

        std::unordered_map<uint32_t, NodeTRS> sampledA;
        std::unordered_map<uint32_t, NodeTRS> sampledB;
        std::set<uint32_t> affectedNodes;

        // Sample A
        for (const auto& ch : animA.channels) {
            if (ch.path == AnimationPath::Weights) continue; // Morph weights blending not implemented yet
            auto& trs = sampledA[ch.targetNode];
            const auto& sampler = animA.samplers[ch.samplerIndex];
            if (ch.path == AnimationPath::Translation) { trs.t = interpolateTranslation(sampler, timeA); trs.hasT = true; }
            else if (ch.path == AnimationPath::Rotation) { trs.r = interpolateRotation(sampler, timeA); trs.hasR = true; }
            else if (ch.path == AnimationPath::Scale) { trs.s = interpolateScale(sampler, timeA); trs.hasS = true; }
            affectedNodes.insert(ch.targetNode);
        }

        // Sample B
        for (const auto& ch : animB.channels) {
            if (ch.path == AnimationPath::Weights) continue;
            auto& trs = sampledB[ch.targetNode];
            const auto& sampler = animB.samplers[ch.samplerIndex];
            if (ch.path == AnimationPath::Translation) { trs.t = interpolateTranslation(sampler, timeB); trs.hasT = true; }
            else if (ch.path == AnimationPath::Rotation) { trs.r = interpolateRotation(sampler, timeB); trs.hasR = true; }
            else if (ch.path == AnimationPath::Scale) { trs.s = interpolateScale(sampler, timeB); trs.hasS = true; }
            affectedNodes.insert(ch.targetNode);
        }

        for (uint32_t nodeIndex : affectedNodes) {
            ecs::Entity entity = static_cast<ecs::Entity>(nodeIndex);
            if (!scene.registry.has<LocalTransform>(entity)) continue;
            glm::mat4& localMat = scene.registry.get<LocalTransform>(entity).matrix;
            
            glm::vec3 t; glm::quat r; glm::vec3 s;
            glm::vec3 skew; glm::vec4 perspective;
            glm::decompose(localMat, s, r, t, skew, perspective);

            auto itA = sampledA.find(nodeIndex);
            auto itB = sampledB.find(nodeIndex);

            glm::vec3 tA = (itA != sampledA.end() && itA->second.hasT) ? itA->second.t : t;
            glm::vec3 tB = (itB != sampledB.end() && itB->second.hasT) ? itB->second.t : t;
            t = glm::mix(tA, tB, weight);

            glm::quat rA = (itA != sampledA.end() && itA->second.hasR) ? itA->second.r : r;
            glm::quat rB = (itB != sampledB.end() && itB->second.hasR) ? itB->second.r : r;
            r = glm::normalize(glm::slerp(rA, rB, weight));

            glm::vec3 sA = (itA != sampledA.end() && itA->second.hasS) ? itA->second.s : s;
            glm::vec3 sB = (itB != sampledB.end() && itB->second.hasS) ? itB->second.s : s;
            s = glm::mix(sA, sB, weight);

            localMat = glm::translate(glm::mat4(1.0f), t) * glm::toMat4(r) * glm::scale(glm::mat4(1.0f), s);
            scene.markAsChanged(entity);
        }
    }

    std::vector<glm::mat4> AnimationSystem::updateSkinning(const ModelDOD& model)
    {
        std::vector<glm::mat4> jointMatrices;
        if (model.skins().empty()) return jointMatrices;

        // Support only first skin for now for simplicity
        const auto& skin = model.skins()[0];
        jointMatrices.resize(skin.joints.size());

        const auto& scene = model.scene();
        
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            ecs::Entity entity = static_cast<ecs::Entity>(skin.joints[i]);
            
            if (scene.registry.has<WorldTransform>(entity)) {
                jointMatrices[i] = scene.registry.get<WorldTransform>(entity).matrix * skin.inverseBindMatrices[i];
            } else {
                jointMatrices[i] = glm::mat4(1.0f);
            }
        }
        return jointMatrices;
    }

    size_t AnimationSystem::getKeyframeIndex(const std::vector<float>& inputs, float time)
    {
        // fastgltf times are sorted. find the first element > time, then step back.
        auto it = std::upper_bound(inputs.begin(), inputs.end(), time);
        if (it == inputs.begin()) return 0;
        return (size_t)std::distance(inputs.begin(), it) - 1;
    }

    // Helper for Cubic Spline calculation
    // p0: starting point, m0: out tangent, p1: ending point, m1: in tangent
    template <typename T>
    static T cubicSpline(float t, T p0, T m0, T p1, T m1)
    {
        float t2 = t * t;
        float t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 +
               (t3 - 2.0f * t2 + t) * m0 +
               (-2.0f * t3 + 3.0f * t2) * p1 +
               (t3 - t2) * m1;
    }

    glm::vec3 AnimationSystem::interpolateTranslation(const AnimationSampler& sampler, float time)
    {
        if (sampler.inputs.empty()) return glm::vec3(0.0f);
        if (sampler.inputs.size() == 1) return glm::vec3(sampler.outputs[0]);

        size_t idx0 = getKeyframeIndex(sampler.inputs, time);
        size_t idx1 = std::min(idx0 + 1, sampler.inputs.size() - 1);

        float t0 = sampler.inputs[idx0];
        float t1 = sampler.inputs[idx1];

        float factor = (t1 != t0) ? (time - t0) / (t1 - t0) : 0.0f;

        if (sampler.interpolation == InterpolationType::Step)
        {
            return glm::vec3(sampler.outputs[idx0]);
        }

        if (sampler.interpolation == InterpolationType::CubicSpline)
        {
            // Cubic Spline layout: [InTangent, Value, OutTangent] per keyframe
            // fastgltf flattens this, so index stride is 3
            // p0 = value at idx0, m0 = outTangent at idx0
            // p1 = value at idx1, m1 = inTangent at idx1

            // Adjust indices for stride of 3
            size_t stride = 3;
            size_t i0 = idx0 * stride;
            size_t i1 = idx1 * stride;

            // output[i*3 + 0] = In Tangent
            // output[i*3 + 1] = Value
            // output[i*3 + 2] = Out Tangent

            glm::vec3 p0 = glm::vec3(sampler.outputs[i0 + 1]);
            glm::vec3 m0 = glm::vec3(sampler.outputs[i0 + 2]) * (t1 - t0); // Scale by delta time
            glm::vec3 p1 = glm::vec3(sampler.outputs[i1 + 1]);
            glm::vec3 m1 = glm::vec3(sampler.outputs[i1 + 0]) * (t1 - t0);

            return cubicSpline(factor, p0, m0, p1, m1);
        }

        // Linear
        return glm::mix(glm::vec3(sampler.outputs[idx0]), glm::vec3(sampler.outputs[idx1]), factor);
    }

    glm::quat AnimationSystem::interpolateRotation(const AnimationSampler& sampler, float time)
    {
        PNKR_PROFILE_FUNCTION();
        if (sampler.inputs.empty()) return glm::quat(1, 0, 0, 0);

        // Handle Step/Linear with stride 1
        if (sampler.interpolation != InterpolationType::CubicSpline)
        {
            if (sampler.inputs.size() == 1)
            {
                auto& v = sampler.outputs[0];
                return glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
            }

            size_t idx0 = getKeyframeIndex(sampler.inputs, time);
            size_t idx1 = std::min(idx0 + 1, sampler.inputs.size() - 1);
            float t0 = sampler.inputs[idx0];
            float t1 = sampler.inputs[idx1];
            float factor = (t1 != t0) ? (time - t0) / (t1 - t0) : 0.0f;

            auto& v0 = sampler.outputs[idx0];
            glm::quat q0(v0.w, v0.x, v0.y, v0.z);

            if (sampler.interpolation == InterpolationType::Step)
            {
                return glm::normalize(q0);
            }

            auto& v1 = sampler.outputs[idx1];
            glm::quat q1(v1.w, v1.x, v1.y, v1.z);
            return glm::normalize(glm::slerp(q0, q1, factor));
        }

        // Cubic Spline (Stride 3)
        size_t idx0 = getKeyframeIndex(sampler.inputs, time);
        size_t idx1 = std::min(idx0 + 1, sampler.inputs.size() - 1);
        float t0 = sampler.inputs[idx0];
        float t1 = sampler.inputs[idx1];
        float factor = (t1 != t0) ? (time - t0) / (t1 - t0) : 0.0f;
        float dt = t1 - t0;

        size_t stride = 3;
        size_t i0 = idx0 * stride;
        size_t i1 = idx1 * stride;

        auto v_p0 = sampler.outputs[i0 + 1];
        auto v_m0 = sampler.outputs[i0 + 2];
        auto v_p1 = sampler.outputs[i1 + 1];
        auto v_m1 = sampler.outputs[i1 + 0];

        glm::quat p0(v_p0.w, v_p0.x, v_p0.y, v_p0.z);
        glm::quat m0(v_m0.w, v_m0.x, v_m0.y, v_m0.z);
        glm::quat p1(v_p1.w, v_p1.x, v_p1.y, v_p1.z);
        glm::quat m1(v_m1.w, v_m1.x, v_m1.y, v_m1.z);

        m0 *= dt;
        m1 *= dt;

        // Spline interpolation for quaternions implies simple math on the components
        // followed by normalization (approximate but standard for glTF).
        glm::quat result;
        result.x = cubicSpline(factor, p0.x, m0.x, p1.x, m1.x);
        result.y = cubicSpline(factor, p0.y, m0.y, p1.y, m1.y);
        result.z = cubicSpline(factor, p0.z, m0.z, p1.z, m1.z);
        result.w = cubicSpline(factor, p0.w, m0.w, p1.w, m1.w);

        return glm::normalize(result);
    }

    glm::vec3 AnimationSystem::interpolateScale(const AnimationSampler& sampler, float time)
    {
        return interpolateTranslation(sampler, time);
    }

    void AnimationSystem::interpolateWeights(const AnimationSampler& sampler, float time, uint32_t numTargets,
                                             float* outWeights)
    {
        if (sampler.inputs.empty() || numTargets == 0) return;

        size_t idx0 = getKeyframeIndex(sampler.inputs, time);
        size_t idx1 = std::min(idx0 + 1, sampler.inputs.size() - 1);

        float t0 = sampler.inputs[idx0];
        float t1 = sampler.inputs[idx1];
        float factor = (t1 != t0) ? (time - t0) / (t1 - t0) : 0.0f;

        for (uint32_t i = 0; i < numTargets; ++i)
        {
            if (sampler.interpolation == InterpolationType::Step)
            {
                outWeights[i] = sampler.outputs[idx0 * numTargets + i].x;
            }
            else if (sampler.interpolation == InterpolationType::CubicSpline)
            {
                float p0 = sampler.outputs[(idx0 * 3 + 1) * numTargets + i].x;
                float m0 = sampler.outputs[(idx0 * 3 + 2) * numTargets + i].x * (t1 - t0);
                float p1 = sampler.outputs[(idx1 * 3 + 1) * numTargets + i].x;
                float m1 = sampler.outputs[(idx1 * 3 + 0) * numTargets + i].x * (t1 - t0);
                outWeights[i] = cubicSpline(factor, p0, m0, p1, m1);
            }
            else
            { // Linear
                float v0 = sampler.outputs[idx0 * numTargets + i].x;
                float v1 = sampler.outputs[idx1 * numTargets + i].x;
                outWeights[i] = glm::mix(v0, v1, factor);
            }
        }
    }

    void AnimationSystem::applyAnimation(ModelDOD& model, const Animation& anim, float time)
    {
        auto& scene = model.scene();

        for (const auto& ch : anim.channels)
        {
            const auto& sampler = anim.samplers[ch.samplerIndex];
            ecs::Entity entity = static_cast<ecs::Entity>(ch.targetNode);

            if (ch.path == AnimationPath::Weights)
            {
                if (scene.registry.has<MeshRenderer>(entity)) {
                    int32_t meshIdx = scene.registry.get<MeshRenderer>(entity).meshID;
                    if (meshIdx >= 0 && (size_t)meshIdx < model.morphTargetInfos().size())
                    {
                        const auto& info = model.morphTargetInfos()[meshIdx];
                        auto& state = model.morphStates()[meshIdx];
                        state.meshIndex = (uint32_t)meshIdx;

                        uint32_t numTargets = (uint32_t)info.targetOffsets.size();
                        std::vector<float> weights(numTargets);
                        interpolateWeights(sampler, time, numTargets, weights.data());

                        for (uint32_t i = 0; i < std::min(8u, numTargets); ++i)
                        {
                            state.activeTargets[i] = info.targetOffsets[i];
                            state.weights[i] = weights[i];
                        }
                    }
                }
                continue;
            }

            if (!scene.registry.has<LocalTransform>(entity)) continue;
            glm::mat4& localMat = scene.registry.get<LocalTransform>(entity).matrix;

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

            // Recompose
            localMat = glm::translate(glm::mat4(1.0f), translation) *
                       glm::toMat4(rotation) *
                       glm::scale(glm::mat4(1.0f), scale);
        }
    }
} // namespace pnkr::renderer::scene
