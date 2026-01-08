#pragma once
#include <cstdint>

namespace pnkr::renderer::scene
{
    enum class SortingType : uint32_t
    {
        Opaque = 0,
        OpaqueDoubleSided = 1,
        Transmission = 2,
        TransmissionDoubleSided = 3,
        Transparent = 4
    };
}
