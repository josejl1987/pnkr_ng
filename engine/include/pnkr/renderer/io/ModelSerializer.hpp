#pragma once

#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/core/result.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/material/Material.hpp"
#include "pnkr/renderer/scene/ModelDOD.hpp"
#include <filesystem>
#include <vector>

namespace pnkr::renderer {
class RHIRenderer;
}

namespace pnkr::renderer::io {
class ModelSerializer {
public:
  static core::Result<void> saveCache(scene::ModelDOD &model,
                                      const std::filesystem::path &path);
  static core::Result<void> loadCache(scene::ModelDOD &model,
                                      const std::filesystem::path &path,
                                      RHIRenderer &renderer);

  static bool savePMESH(const assets::ImportedModel &model,
                        const std::filesystem::path &path);
  static bool loadPMESH(assets::ImportedModel &model,
                        const std::filesystem::path &path);

  static scene::MaterialCPU
  toMaterialCPU(const MaterialData &md,
                const std::vector<TexturePtr> &textures);
  static MaterialData fromMaterialCPU(const scene::MaterialCPU &mc,
                                      const std::vector<TexturePtr> &textures);
};
} // namespace pnkr::renderer::io
