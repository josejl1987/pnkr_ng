//
// Created by Jose on 12/13/2025.
//

#ifndef PNKR_HANDLE_H
#define PNKR_HANDLE_H

#include <cstdint>
#include <limits>

using Handle = uint32_t;
using MeshHandle = Handle;
using PipelineHandle = Handle;
using TextureHandle = Handle;

constexpr Handle INVALID_HANDLE = std::numeric_limits<Handle>::max();

#endif // PNKR_HANDLE_H
