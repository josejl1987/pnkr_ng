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
        glm::vec4 m_baseColorFactor{1.0f};
        glm::vec3 m_emissiveFactor{0.0f};
        float m_metallicFactor{1.0f};
        float m_roughnessFactor{1.0f};
        float m_alphaCutoff{0.5f};
        float m_normalScale{1.0f};
        float m_occlusionStrength{1.0f};
        uint32_t m_alphaMode{0};

        glm::vec3 m_specularFactor{1.0f};
        float m_glossinessFactor{1.0f};
        bool m_isSpecularGlossiness{false};

        TextureHandle m_baseColorTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_normalTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_metallicRoughnessTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_occlusionTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_emissiveTexture = INVALID_TEXTURE_HANDLE;

        uint32_t m_baseColorUV{0};
        uint32_t m_normalUV{0};
        uint32_t m_metallicRoughnessUV{0};
        uint32_t m_occlusionUV{0};
        uint32_t m_emissiveUV{0};

        rhi::SamplerAddressMode m_baseColorSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_normalSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_metallicRoughnessSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_occlusionSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_emissiveSampler{rhi::SamplerAddressMode::Repeat};
    };

    struct MeshPrimitive
    {
        MeshHandle m_mesh;
        uint32_t m_materialIndex;
        uint64_t m_vertexBufferAddress;
    };

    struct Node
    {
        std::string m_name;
        Transform m_localTransform;
        Transform m_worldTransform;
        int m_parentIndex = -1;
        std::vector<int> m_children;
        std::vector<MeshPrimitive> m_meshPrimitives;
    };

    class Model
    {
    public:
        static std::unique_ptr<Model> load(RHIRenderer& renderer, const std::filesystem::path& path, bool vertexPulling = false);
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
