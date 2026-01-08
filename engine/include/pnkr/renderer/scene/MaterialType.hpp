#pragma once
#include <cstdint>

namespace pnkr::renderer::scene {

enum class MaterialType : uint32_t {
    metallic_roughness  = 1u << 0,
    specular_glossiness = 1u << 1,
    sheen              = 1u << 2,
    clear_coat         = 1u << 3,
    specular           = 1u << 4,
    transmission       = 1u << 5,
    volume             = 1u << 6,
    unlit              = 1u << 7,
    double_sided       = 1u << 8,
    anisotropy         = 1u << 9,
    iridescence        = 1u << 10,
};

constexpr uint32_t materialMask(MaterialType f) {
    return static_cast<uint32_t>(f);
}

constexpr uint32_t operator|(MaterialType a, MaterialType b) {
    return materialMask(a) | materialMask(b);
}

constexpr uint32_t& operator|=(uint32_t& mask, MaterialType f) {
    mask |= materialMask(f);
    return mask;
}

inline bool hasFlag(uint32_t mask, MaterialType f) {
    return (mask & materialMask(f)) == materialMask(f);
}

}
