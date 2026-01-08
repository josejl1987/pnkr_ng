#pragma once

#include <cstdint>
#include <limits>
#include <compare>

namespace pnkr::core {

template <typename Tag>
struct Handle {
    uint32_t index : 20;
    uint32_t generation : 12;

    static constexpr uint32_t kInvalidIndex = (1 << 20) - 1;

    constexpr Handle() noexcept : index(kInvalidIndex), generation(0) {}
    constexpr Handle(uint32_t idx, uint32_t gen) noexcept : index(idx), generation(gen) {}

    constexpr bool isValid() const noexcept { return index != kInvalidIndex; }
    constexpr void invalidate() noexcept { index = kInvalidIndex; }

    static const Handle Null;

    auto operator<=>(const Handle&) const = default;
    explicit operator bool() const noexcept { return isValid(); }
};

template<typename Tag>
const Handle<Tag> Handle<Tag>::Null = { kInvalidIndex, 0 };

struct MeshTag {};
struct PipelineTag {};
struct TextureTag {};
struct BufferTag {};

}

using MeshHandle = pnkr::core::Handle<pnkr::core::MeshTag>;
using PipelineHandle = pnkr::core::Handle<pnkr::core::PipelineTag>;
using TextureHandle = pnkr::core::Handle<pnkr::core::TextureTag>;
using BufferHandle = pnkr::core::Handle<pnkr::core::BufferTag>;

constexpr uint32_t INVALID_ID = std::numeric_limits<uint32_t>::max();
inline constexpr MeshHandle INVALID_MESH_HANDLE{};
inline constexpr PipelineHandle INVALID_PIPELINE_HANDLE{};
inline constexpr TextureHandle INVALID_TEXTURE_HANDLE{};
inline constexpr BufferHandle INVALID_BUFFER_HANDLE{};

namespace std {
template <typename Tag>
struct hash<pnkr::core::Handle<Tag>> {
    size_t operator()(const pnkr::core::Handle<Tag>& h) const noexcept {
        const uint32_t packed = (h.generation << 20) | h.index;
        return hash<uint32_t>{}(packed);
    }
};
}
