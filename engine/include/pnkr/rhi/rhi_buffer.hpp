#pragma once

#include "rhi_types.hpp"
#include <cstdint>
#include <string>
#include <span>
#include <cstddef>

namespace pnkr::renderer::rhi
{
    struct BufferDescriptor
    {
        uint64_t size = 0;
        BufferUsageFlags usage = BufferUsage::None;
        MemoryUsage memoryUsage = MemoryUsage::GPUOnly;
        const void* data = nullptr;
        std::string debugName;
    };

    class RHIBuffer
    {
    public:
        RHIBuffer() = default;
        virtual ~RHIBuffer() = default;

        RHIBuffer(const RHIBuffer&) = delete;
        RHIBuffer& operator=(const RHIBuffer&) = delete;

        RHIBuffer(RHIBuffer&&) noexcept = default;
        RHIBuffer& operator=(RHIBuffer&&) noexcept = default;

        virtual std::byte* map() = 0;
        virtual void unmap() = 0;
        virtual void flush(uint64_t offset, uint64_t size) = 0;
        virtual void invalidate(uint64_t offset, uint64_t size) = 0;

        virtual void uploadData(std::span<const std::byte> data, uint64_t offset = 0) = 0;

        virtual uint64_t size() const = 0;
        virtual BufferUsageFlags usage() const = 0;
        virtual MemoryUsage memoryUsage() const = 0;

        virtual void* nativeHandle() const = 0;
        virtual uint64_t getDeviceAddress() const = 0;

        void setBindlessHandle(BufferBindlessHandle handle) { m_bindlessHandle = handle; }
        BufferBindlessHandle getBindlessHandle() const { return m_bindlessHandle; }

        void setDebugName(std::string name) { m_debugName = std::move(name); }
        const std::string& debugName() const { return m_debugName; }

    protected:
        BufferBindlessHandle m_bindlessHandle;
        std::string m_debugName;
    };

}
