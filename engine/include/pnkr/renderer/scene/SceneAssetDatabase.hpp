#pragma once

#include <vector>
#include <string>
#include <memory>
#include <span>

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/material/Material.hpp"
#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/GltfCamera.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"

namespace pnkr::renderer {
    class RHIRenderer;
}

namespace pnkr::renderer::scene
{
    using MaterialCPU = pnkr::assets::ImportedMaterial;
    using MaterialData = pnkr::renderer::MaterialData;

    struct PrimitiveDOD
    {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        uint32_t materialIndex = 0;
    };

    struct MeshDOD
    {
        std::vector<PrimitiveDOD> primitives;
        std::string name;
    };

    struct MorphTargetInfo
    {
        uint32_t meshIndex = ~0u;
        std::vector<uint32_t> targetOffsets;
    };

    class SceneAssetDatabase
    {
    public:
        SceneAssetDatabase() = default;
        ~SceneAssetDatabase() = default;

        // Accessors
        const std::vector<MaterialData>& materials() const { return m_materials; }
        std::vector<MaterialData>& materialsMutable() { return m_materials; }

        const std::vector<TextureHandle>& textures() const { return m_textures; }
        std::vector<TextureHandle>& texturesMutable() { return m_textures; }

        const std::vector<TextureHandle>& pendingTextures() const { return m_pendingTextures; }
        std::vector<TextureHandle>& pendingTexturesMutable() { return m_pendingTextures; }

        const std::vector<MeshDOD>& meshes() const { return m_meshes; }
        std::vector<MeshDOD>& meshesMutable() { return m_meshes; }

        const std::vector<BoundingBox>& meshBounds() const { return m_meshBounds; }
        std::vector<BoundingBox>& meshBoundsMutable() { return m_meshBounds; }

        const std::vector<Skin>& skins() const { return m_skins; }
        std::vector<Skin>& skinsMutable() { return m_skins; }

        const std::vector<Animation>& animations() const { return m_animations; }
        std::vector<Animation>& animationsMutable() { return m_animations; }

        const std::vector<GltfCamera>& cameras() const { return m_cameras; }
        std::vector<GltfCamera>& camerasMutable() { return m_cameras; }

        const std::vector<MorphTargetInfo>& morphTargetInfos() const { return m_morphTargetInfos; }
        std::vector<MorphTargetInfo>& morphTargetInfosMutable() { return m_morphTargetInfos; }

        // CPU Geometry buffers
        const std::vector<Vertex>& cpuVertices() const { return m_cpuVertices; }
        std::vector<Vertex>& cpuVerticesMutable() { return m_cpuVertices; }

        const std::vector<uint32_t>& cpuIndices() const { return m_cpuIndices; }
        std::vector<uint32_t>& cpuIndicesMutable() { return m_cpuIndices; }

        // Source Data (likely to be dropped after load)
        const std::vector<MaterialCPU>& materialsCPU() const { return m_materialsCPU; }
        std::vector<MaterialCPU>& materialsCPUMutable() { return m_materialsCPU; }

        const std::vector<std::string>& textureFiles() const { return m_textureFiles; }
        std::vector<std::string>& textureFilesMutable() { return m_textureFiles; }

        const std::vector<uint8_t>& textureIsSrgb() const { return m_textureIsSrgb; }
        std::vector<uint8_t>& textureIsSrgbMutable() { return m_textureIsSrgb; }

        // Methods logic moved from ModelDOD
        uint32_t appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                         uint32_t materialIndex,
                                         const std::string& name);

        void dropCpuGeometry();

        void uploadUnifiedBuffers(RHIRenderer& renderer);

        BufferPtr vertexBuffer;
        BufferPtr indexBuffer;
        BufferPtr boundsBuffer;

    private:
        std::vector<MaterialData> m_materials;
        std::vector<TextureHandle> m_textures;
        std::vector<TextureHandle> m_pendingTextures;
        std::vector<MeshDOD> m_meshes;
        std::vector<BoundingBox> m_meshBounds;
        std::vector<Skin> m_skins;
        std::vector<Animation> m_animations;
        std::vector<GltfCamera> m_cameras;
        std::vector<MorphTargetInfo> m_morphTargetInfos;

        std::vector<Vertex> m_cpuVertices;
        std::vector<uint32_t> m_cpuIndices;

        std::vector<MaterialCPU> m_materialsCPU;
        std::vector<std::string> m_textureFiles;
        std::vector<uint8_t> m_textureIsSrgb;
    };
}
