#pragma once
#include <vector>
#include <string>
#include <filesystem>
#include <memory>
#include "Node.hpp"
#include "pnkr/renderer/material/Material.hpp"
#include "pnkr/core/Handle.h"

namespace pnkr::renderer {
    class Renderer;
}

namespace pnkr::renderer::scene {

class Model {
public:
    static std::unique_ptr<Model> load(Renderer& renderer, const std::filesystem::path& path);

    const std::vector<Node>& nodes() const { return m_nodes; }
    const std::vector<MaterialData>& materials() const { return m_materials; }
    const std::vector<TextureHandle>& textures() const { return m_textures; }

    void updateTransforms();

private:
    std::vector<Node> m_nodes;
    std::vector<MaterialData> m_materials;
    std::vector<TextureHandle> m_textures;

    std::vector<int> m_rootNodes;
};

}

