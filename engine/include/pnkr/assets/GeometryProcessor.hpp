#pragma once
#include "pnkr/assets/ImportedData.hpp"

namespace pnkr::assets {
    struct GeometryProcessor {
        static void generateTangents(ImportedPrimitive& prim);
    };
}
