//
// Created by Jose on 12/13/2025.
//

#pragma once

#include <cstdint>
#include <limits>
#include <compare>

namespace pnkr::core {

template <typename Tag>
struct Handle {
    uint32_t id = std::numeric_limits<uint32_t>::max();

    constexpr bool isValid() const { return id != std::numeric_limits<uint32_t>::max(); }
    constexpr void invalidate() { id = std::numeric_limits<uint32_t>::max(); }

    auto operator<=>(const Handle&) const = default;
    explicit operator bool() const { return isValid(); }
};

struct MeshTag {};
struct PipelineTag {};
struct TextureTag {};
struct BufferTag {};

} // namespace pnkr::core

using MeshHandle = pnkr::core::Handle<pnkr::core::MeshTag>;
using PipelineHandle = pnkr::core::Handle<pnkr::core::PipelineTag>;
using TextureHandle = pnkr::core::Handle<pnkr::core::TextureTag>;
using BufferHandle = pnkr::core::Handle<pnkr::core::BufferTag>;

constexpr uint32_t INVALID_ID = std::numeric_limits<uint32_t>::max();
inline constexpr MeshHandle INVALID_MESH_HANDLE{};
inline constexpr PipelineHandle INVALID_PIPELINE_HANDLE{};
inline constexpr TextureHandle INVALID_TEXTURE_HANDLE{};
inline constexpr BufferHandle INVALID_BUFFER_HANDLE{};
