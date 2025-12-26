#pragma once

#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/scene/Animation.hpp"

#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <limits>

namespace pnkr::renderer::scene
{
    enum MaterialFlags : uint32_t {
        Material_CastShadow    = 0x1,
        Material_ReceiveShadow = 0x2,
        Material_Transparent   = 0x4,
        Material_Unlit         = 0x8,
        Material_SpecularGlossiness = 0x10,
        Material_DoubleSided   = 0x20,
    };

    struct MaterialCPU
    {
        float baseColorFactor[4]  = {1,1,1,1};
        float emissiveFactor[4]   = {0,0,0,0}; // w is strength

        float metallic            = 1.0f;
        float roughness           = 1.0f;
        float alphaCutoff         = 0.5f;
        float ior                 = 1.5f;

        float transmissionFactor  = 0.0f;
        float clearcoatFactor     = 0.0f;
        float clearcoatRoughness  = 0.0f;
        float clearcoatNormalScale = 1.0f;

        float specularFactorScalar = 1.0f;
        float specularColorFactor[3] = {1,1,1};
        
        float sheenColorFactor[3] = {0,0,0};
        float sheenRoughnessFactor = 0.0f;

        float volumeThicknessFactor = 0.0f;
        float volumeAttenuationDistance = 1e30f; // infinity substitute
        float volumeAttenuationColor[3] = {1,1,1};

        int32_t baseColorTex      = -1;
        int32_t normalTex         = -1;
        int32_t metallicRoughnessTex = -1;
        int32_t occlusionTex      = -1;
        int32_t emissiveTex       = -1;
        int32_t clearcoatTex      = -1;
        int32_t clearcoatRoughnessTex = -1;
        int32_t clearcoatNormalTex = -1;
        int32_t specularTex       = -1;
        int32_t specularColorTex  = -1;
        int32_t transmissionTex   = -1;
        int32_t sheenColorTex     = -1;
        int32_t sheenRoughnessTex = -1;
        int32_t volumeThicknessTex = -1;

        uint32_t flags            = Material_CastShadow | Material_ReceiveShadow;
    };
    static_assert(std::is_trivially_copyable_v<MaterialCPU>);

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

        // IOR + Emissive Strength
        float m_ior{1.5f};
        float m_emissiveStrength{1.0f};

        // Transmission
        float m_transmissionFactor{0.0f};

        // Clearcoat
        float m_clearcoatFactor{0.0f};
        float m_clearcoatRoughnessFactor{0.0f};
        float m_clearcoatNormalScale{1.0f};

        // KHR_materials_specular
        float m_specularFactorScalar{1.0f};
        glm::vec3 m_specularColorFactor{1.0f};
        bool m_hasSpecular{false};

        // Specular/Glossiness workflow
        glm::vec3 m_specularFactor{1.0f};
        float m_glossinessFactor{1.0f};
        bool m_isSpecularGlossiness{false};
        bool m_isUnlit{false};

        // Sheen
        glm::vec3 m_sheenColorFactor{0.0f};
        float m_sheenRoughnessFactor{0.0f};

        // Volume
        float m_volumeThicknessFactor{0.0f};
        float m_volumeAttenuationDistance{std::numeric_limits<float>::infinity()};
        glm::vec3 m_volumeAttenuationColor{1.0f};

        // NEW: Double Sided support
        bool m_doubleSided{false};

        TextureHandle m_baseColorTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_normalTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_metallicRoughnessTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_occlusionTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_emissiveTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_clearcoatTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_clearcoatRoughnessTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_clearcoatNormalTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_specularTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_specularColorTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_transmissionTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_sheenColorTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_sheenRoughnessTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_volumeThicknessTexture = INVALID_TEXTURE_HANDLE;

        uint32_t m_baseColorUV{0};
        uint32_t m_normalUV{0};
        uint32_t m_metallicRoughnessUV{0};
        uint32_t m_occlusionUV{0};
        uint32_t m_emissiveUV{0};
        uint32_t m_clearcoatUV{0};
        uint32_t m_clearcoatRoughnessUV{0};
        uint32_t m_clearcoatNormalUV{0};
        uint32_t m_specularUV{0};
        uint32_t m_specularColorUV{0};
        uint32_t m_transmissionUV{0};
        uint32_t m_sheenColorUV{0};
        uint32_t m_sheenRoughnessUV{0};
        uint32_t m_volumeThicknessUV{0};

        rhi::SamplerAddressMode m_baseColorSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_normalSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_metallicRoughnessSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_occlusionSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_emissiveSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_clearcoatSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_clearcoatRoughnessSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_clearcoatNormalSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_specularSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_specularColorSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_transmissionSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_sheenColorSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_sheenRoughnessSampler{rhi::SamplerAddressMode::Repeat};
        rhi::SamplerAddressMode m_volumeThicknessSampler{rhi::SamplerAddressMode::Repeat};
    };

    enum class LightType
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    struct Light
    {
        std::string m_name;
        LightType m_type = LightType::Directional;
        glm::vec3 m_color{1.0f};
        glm::vec3 m_direction{0.0f, 0.0f, -1.0f};
        float m_intensity{1.0f};
        float m_range{0.0f}; // 0 = infinite
        float m_innerConeAngle{0.0f}; // Radians
        float m_outerConeAngle{0.785398f}; // Radians (45 degrees)
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
        int m_lightIndex = -1;
    };

    class Model
    {
    public:
        static std::unique_ptr<Model> load(RHIRenderer& renderer, const std::filesystem::path& path,
                                           bool vertexPulling = false);
        void updateTransforms();

        const std::vector<MaterialData>& materials() const { return m_materials; }
        std::vector<MaterialData>& materialsMutable() { return m_materials; }
        const std::vector<Node>& nodes() const { return m_nodes; }
        std::vector<Node>& nodesMutable() { return m_nodes; }
        const std::vector<int>& rootNodes() const { return m_rootNodes; }
        const std::vector<Light>& lights() const { return m_lights; }
        std::vector<Light>& lightsMutable() { return m_lights; }

        const std::vector<Skin>& skins() const { return m_skins; }
        const std::vector<Animation>& animations() const { return m_animations; }

    private:
        std::vector<TextureHandle> m_textures;
        std::vector<MaterialData> m_materials;
        std::vector<Node> m_nodes;
        std::vector<int> m_rootNodes;
        std::vector<Light> m_lights;
        std::vector<Skin> m_skins;
        std::vector<Animation> m_animations;
    };
}
