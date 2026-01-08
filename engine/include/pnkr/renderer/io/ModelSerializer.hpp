#pragma once

#include "pnkr/renderer/scene/ModelDOD.hpp"
#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/core/result.hpp"
#include <filesystem>
#include <vector>

namespace pnkr::renderer
{
    class RHIRenderer;
}

namespace pnkr::renderer::io
{
    class ModelSerializer
    {
    public:
        static core::Result<void> saveCache(scene::ModelDOD& model, const std::filesystem::path& path);
        static core::Result<void> loadCache(scene::ModelDOD& model, const std::filesystem::path& path, RHIRenderer& renderer);

        static bool savePMESH(const assets::ImportedModel& model, const std::filesystem::path& path);
        static bool loadPMESH(assets::ImportedModel& model, const std::filesystem::path& path);

        static scene::MaterialCPU toMaterialCPU(const MaterialData& md, const std::vector<TextureHandle>& textures);
        static MaterialData fromMaterialCPU(const scene::MaterialCPU& mc, const std::vector<TextureHandle>& textures);
    };
}
