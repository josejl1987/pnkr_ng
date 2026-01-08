#pragma once
#include "pnkr/assets/ImportedData.hpp"
#include <memory>

namespace pnkr::renderer {
    class RHIRenderer;
}

namespace pnkr::renderer::scene {
    class ModelDOD;
}

namespace pnkr::renderer::io {

    class ModelUploader {
    public:
        static std::unique_ptr<scene::ModelDOD> upload(
            RHIRenderer& renderer,
            assets::ImportedModel&& source);
    };

}
