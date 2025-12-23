#pragma once
#include <cstdint>

namespace pnkr::renderer::scene {

enum MaterialType : uint32_t {
    MaterialType_MetallicRoughness  = 1u << 0, // 0x01
    MaterialType_SpecularGlossiness = 1u << 1, // 0x02
    MaterialType_Sheen              = 1u << 2, // 0x04
    MaterialType_ClearCoat          = 1u << 3, // 0x08
    MaterialType_Specular           = 1u << 4, // 0x10
    MaterialType_Transmission       = 1u << 5, // 0x20
    MaterialType_Volume             = 1u << 6, // 0x40
    MaterialType_Unlit              = 1u << 7, // 0x80
};

inline bool hasFlag(uint32_t mask, MaterialType f) {
    return (mask & uint32_t(f)) == uint32_t(f);
}

} // namespace pnkr::renderer::scene
