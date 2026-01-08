#pragma once

#include <cstdint>
#include "pnkr/renderer/GPUBufferSlice.hpp"

namespace pnkr::renderer
{
    inline uint64_t addrOrZero(const GPUBufferSlice& s)
    {
        return s.deviceAddress;
    }
}
