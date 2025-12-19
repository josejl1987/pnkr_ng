#pragma once

#include "rhi_types.hpp"
#include <cstdint>

namespace pnkr::renderer::rhi
{
    struct BufferDescriptor
    {
        uint64_t size;
        BufferUsage usage;
        MemoryUsage memoryUsage;
        const char* debugName = nullptr;
    };

    class RHIBuffer
    {
    public:
        virtual ~RHIBuffer() = default;

        // Map/unmap for CPU access
        virtual void* map() = 0;
        virtual void unmap() = 0;

        // Upload data (convenience for map/memcpy/unmap)
        virtual void uploadData(const void* data, uint64_t size, uint64_t offset = 0) = 0;

        // Getters
        virtual uint64_t size() const = 0;
        virtual BufferUsage usage() const = 0;
        virtual MemoryUsage memoryUsage() const = 0;

        // Backend-specific handle (for interop)
        virtual void* nativeHandle() const = 0;
        virtual uint64_t getDeviceAddress() const = 0;
    };

} // namespace pnkr::renderer::rhi
