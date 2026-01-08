#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/Light.hpp"
#include "pnkr/renderer/scene/GltfCamera.hpp"
#include "pnkr/rhi/rhi_types.hpp"

namespace pnkr::assets {

    enum class LoadPriority {
        Thumbnail = 0,
        Low,
        Medium,
        High,
        Immediate
    };

    struct ImportedTexture {
        std::string sourcePath;
        bool isSrgb = false;
        bool isKtx = false;
        LoadPriority priority = LoadPriority::Medium;
    };

    struct ImportedPrimitive {
        std::vector<renderer::Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
        glm::vec3 minPos;
        glm::vec3 maxPos;

        struct MorphTarget {
            std::vector<glm::vec3> positionDeltas;
            std::vector<glm::vec3> normalDeltas;
            std::vector<glm::vec3> tangentDeltas;
        };
        std::vector<MorphTarget> targets;
    };

    struct ImportedTextureSlot {
        int32_t textureIndex = -1;
        renderer::rhi::SamplerAddressMode sampler = renderer::rhi::SamplerAddressMode::Repeat;
        uint32_t uvChannel = 0;
        glm::vec4 transform{0.0f, 0.0f, 1.0f, 1.0f};
    };

    struct ImportedMesh {
        std::string name;
        std::vector<ImportedPrimitive> primitives;
    };

    struct ImportedMaterial {
        glm::vec4 baseColorFactor{1.0f};
        glm::vec3 emissiveFactor{0.0f};
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        float alphaCutoff = 0.5f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float ior = 1.5f;
        float emissiveStrength = 1.0f;
        float transmissionFactor = 0.0f;
        float clearcoatFactor = 0.0f;
        float clearcoatRoughnessFactor = 0.0f;
        float clearcoatNormalScale = 1.0f;
        float specularFactorScalar = 1.0f;
        glm::vec3 specularColorFactor{1.0f};
        bool hasSpecular = false;
        glm::vec3 specularFactor{1.0f};
        float glossinessFactor = 1.0f;
        bool isSpecularGlossiness = false;
        bool isUnlit = false;
        glm::vec3 sheenColorFactor{0.0f};
        float sheenRoughnessFactor = 0.0f;
        float anisotropyFactor = 0.0f;
        float anisotropyRotation = 0.0f;
        float iridescenceFactor = 0.0f;
        float iridescenceIor = 1.3f;
        float iridescenceThicknessMinimum = 100.0f;
        float iridescenceThicknessMaximum = 400.0f;
        float volumeThicknessFactor = 0.0f;
        float volumeAttenuationDistance = std::numeric_limits<float>::infinity();
        glm::vec3 volumeAttenuationColor{1.0f};
        bool doubleSided = false;
        uint32_t alphaMode = 0;

        ImportedTextureSlot baseColor;
        ImportedTextureSlot normal;
        ImportedTextureSlot metallicRoughness;
        ImportedTextureSlot occlusion;
        ImportedTextureSlot emissive;
        ImportedTextureSlot clearcoat;
        ImportedTextureSlot clearcoatRoughness;
        ImportedTextureSlot clearcoatNormal;
        ImportedTextureSlot specular;
        ImportedTextureSlot specularColor;
        ImportedTextureSlot transmission;
        ImportedTextureSlot sheenColor;
        ImportedTextureSlot sheenRoughness;
        ImportedTextureSlot anisotropy;
        ImportedTextureSlot iridescence;
        ImportedTextureSlot iridescenceThickness;
        ImportedTextureSlot volumeThickness;
    };

    struct ImportedNode {
        std::string name;
        glm::mat4 localTransform{1.0f};
        int parentIndex = -1;
        std::vector<int> children;
        int meshIndex = -1;
        int lightIndex = -1;
        int cameraIndex = -1;
        int skinIndex = -1;
    };

    struct ImportedModel {
        std::vector<ImportedTexture> textures;
        std::vector<ImportedMaterial> materials;
        std::vector<ImportedMesh> meshes;
        std::vector<ImportedNode> nodes;
        std::vector<renderer::scene::Animation> animations;
        std::vector<renderer::scene::Skin> skins;
        std::vector<renderer::scene::Light> lights;
        std::vector<renderer::scene::GltfCamera> cameras;
        std::vector<int> rootNodes;
    };
}
