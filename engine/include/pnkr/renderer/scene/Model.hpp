#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/transform.hpp"

#include <vector>
#include <string>
#include <memory>
#include <filesystem>

namespace pnkr::renderer::scene
{
    struct MaterialData
    {
        glm::vec4 baseColorFactor{1.0f};
        TextureHandle baseColorTexture = INVALID_TEXTURE_HANDLE;
    };

    struct MeshPrimitive
    {
        MeshHandle mesh;
        uint32_t materialIndex;
    };

    struct Node
    {
        std::string name;
        Transform localTransform;
        Transform worldTransform;
        int parentIndex = -1;
        std::vector<int> children;
        std::vector<MeshPrimitive> meshPrimitives;
    };

    class Model
    {
    public:
        static std::unique_ptr<Model> load(RHIRenderer& renderer, const std::filesystem::path& path);

        void updateTransforms();

        const std::vector<MaterialData>& materials() const { return m_materials; }
        const std::vector<Node>& nodes() const { return m_nodes; }
        const std::vector<int>& rootNodes() const { return m_rootNodes; }

    private:
        std::vector<TextureHandle> m_textures;
        std::vector<MaterialData> m_materials;
        std::vector<Node> m_nodes;
        std::vector<int> m_rootNodes;
    };
}
