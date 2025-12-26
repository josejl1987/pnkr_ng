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

        void setBindlessHandle(BindlessHandle handle) { m_bindlessHandle = handle; }
        BindlessHandle getBindlessHandle() const { return m_bindlessHandle; }

    protected:
        BindlessHandle m_bindlessHandle;
    };

} // namespace pnkr::renderer::rhi
