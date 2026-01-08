#pragma once

#include <cstddef>
#include <cstdint>

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer
{

    struct GPUBufferSlice
    {
        BufferPtr    buffer;
        std::size_t  offset = 0;
        std::size_t  size   = 0;
        std::size_t  dataOffset = 0;
        uint64_t     deviceAddress = 0;

        explicit operator bool() const { return deviceAddress != 0; }

        uint64_t payloadAddress() const { return deviceAddress + dataOffset; }
    };

    inline GPUBufferSlice makeSlice(const RHIRenderer& r, BufferPtr buf,
                                    std::size_t off, std::size_t sz,
                                    std::size_t dataOff = 0)
    {
        GPUBufferSlice s{};
        s.buffer = buf;
        s.offset = off;
        s.size   = sz;
        s.dataOffset = dataOff;

        const uint64_t base = r.getBufferDeviceAddress(buf.handle());
        s.deviceAddress = base ? (base + static_cast<uint64_t>(off)) : 0ull;
        return s;
    }
}
