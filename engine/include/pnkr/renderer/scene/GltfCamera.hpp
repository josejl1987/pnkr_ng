#pragma once

#include <string>
#include <cstdint>

namespace pnkr::renderer::scene
{
    struct GltfCamera
    {
        enum class Type : uint8_t { Perspective = 0, Orthographic = 1 };

        std::string name;
        Type type = Type::Perspective;

        // Perspective
        float yfovRad = 0.78539816339f; // ~45 deg
        float aspectRatio = 0.0f;       // 0 => use viewport aspect
        float znear = 0.1f;
        float zfar  = 0.0f;             // 0 => treat as "infinite" / clamp to a large value

        // Orthographic
        float xmag = 1.0f;
        float ymag = 1.0f;
    };
}
