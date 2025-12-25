#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace pnkr::renderer::scene
{
    // --- Skeleton Structures ---
    struct Skin
    {
        std::string name;
        std::vector<glm::mat4> inverseBindMatrices; // Configured pose -> local bone space
        std::vector<uint32_t> joints;               // Indices into node hierarchy
        int skeletonRootNode = -1;
    };

    // --- Animation Structures ---
    enum class InterpolationType
    {
        Linear,
        Step,
        CubicSpline
    };

    enum class AnimationPath
    {
        Translation,
        Rotation,
        Scale,
        Weights
    };

    struct AnimationSampler
    {
        InterpolationType interpolation;
        std::vector<float> inputs;      // Time keys
        std::vector<glm::vec4> outputs; // Data (Vec3 position/scale stored as Vec4, Quat stored as Vec4)
    };

    struct AnimationChannel
    {
        int samplerIndex;
        uint32_t targetNode; // Index into node hierarchy
        AnimationPath path;
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float duration = 0.0f;
    };
} // namespace pnkr::renderer::scene
