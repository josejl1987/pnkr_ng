#pragma once

#include <cstdint>

namespace pnkr::renderer::rhi
{
    class RHIFence
    {
    public:
        virtual ~RHIFence() = default;

        virtual bool wait(uint64_t timeout = UINT64_MAX) = 0;
        virtual void reset() = 0;
        virtual bool isSignaled() const = 0;
        virtual void* nativeHandle() const = 0;
    };
}
