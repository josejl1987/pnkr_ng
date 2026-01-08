#include "pnkr/renderer/io/GLTFLoader.hpp"
#include "pnkr/assets/AssetImporter.hpp"
#include "pnkr/renderer/io/ModelUploader.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/core/profiler.hpp"

namespace pnkr::renderer::io
{
    std::unique_ptr<scene::ModelDOD> GLTFLoader::load(RHIRenderer& renderer, const std::filesystem::path& path, bool vertexPulling)
    {
        PNKR_PROFILE_FUNCTION();
        (void)vertexPulling;

        auto importedModel = assets::AssetImporter::loadGLTF(path);
        if (!importedModel) {
            return nullptr;
        }

        return ModelUploader::upload(renderer, std::move(*importedModel));
    }
}
