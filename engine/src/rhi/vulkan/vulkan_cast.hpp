#pragma once

#include <cassert>

namespace pnkr::renderer::rhi::vulkan
{
    template <typename Derived, typename Base>
    inline Derived* rhi_cast(Base* ptr)
    {
#ifdef NDEBUG
        // RELEASE: Zero overhead. Assumes you know what you are doing.
        return static_cast<Derived*>(ptr);
#else
        // DEBUG: Keeps the safety check.
        if (!ptr) return nullptr;
        
        auto* ret = dynamic_cast<Derived*>(ptr);
        assert(ret && "RHI Type Mismatch! Attempted to cast to wrong backend type.");
        return ret;
#endif
    }
}
