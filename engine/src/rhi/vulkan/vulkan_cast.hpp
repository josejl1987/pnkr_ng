#pragma once

#include <cassert>

namespace pnkr::renderer::rhi::vulkan
{
    template <typename Derived, typename Base>
    inline Derived* rhi_cast(Base* ptr)
    {
#ifdef NDEBUG

        return static_cast<Derived*>(ptr);
#else

        auto* ret = dynamic_cast<Derived*>(ptr);
        assert(ret || !ptr);
        return ret;
#endif
    }
}
