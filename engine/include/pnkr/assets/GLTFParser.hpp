#pragma once
#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/assets/LoadProgress.hpp"
#include <fastgltf/core.hpp>

namespace pnkr::assets {

    class GLTFParser {
    public:
        static void populateModel(ImportedModel& model, const fastgltf::Asset& gltf, LoadProgress* progress);
    };

}
