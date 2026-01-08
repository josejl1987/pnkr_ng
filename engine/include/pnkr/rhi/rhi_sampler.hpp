#pragma once

#include "rhi_types.hpp"

namespace pnkr::renderer::rhi
{
    class RHISampler
    {
    public:
        virtual ~RHISampler() = default;

        virtual void* nativeHandle() const = 0;

        void setBindlessHandle(SamplerBindlessHandle handle) { m_bindlessHandle = handle; }
        SamplerBindlessHandle getBindlessHandle() const { return m_bindlessHandle; }

        void setDebugName(std::string name) { m_debugName = std::move(name); }
        const std::string& debugName() const { return m_debugName; }

    protected:
        SamplerBindlessHandle m_bindlessHandle;
        std::string m_debugName;
    };

}
