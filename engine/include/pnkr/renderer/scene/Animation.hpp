#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace pnkr::renderer::scene
{

    struct Skin
    {
        std::string name;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<uint32_t> joints;
        int skeletonRootNode = -1;
    };

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
        std::vector<float> inputs;
        std::vector<glm::vec4> outputs;
    };

    struct AnimationChannel
    {
        int samplerIndex;
        uint32_t targetNode;
        AnimationPath path;
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float duration = 0.0f;
    };
}
