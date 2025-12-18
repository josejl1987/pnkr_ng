#pragma once

#include "rhi_types.hpp"

namespace pnkr::renderer::rhi
{
    class RHISampler
    {
    public:
        virtual ~RHISampler() = default;

        // Backend-specific handle
        virtual void* nativeHandle() const = 0;
    };

} // namespace pnkr::renderer::rhi
