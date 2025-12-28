#pragma once

#include "pnkr/renderer/scene/Model.hpp" // Reuse MaterialData/Light definitions
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/VtxData.hpp"
#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/GltfCamera.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include <filesystem>
#include <vector>
#include <memory>

namespace pnkr::renderer::scene
{
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

    struct AnimationState
    {
        uint32_t animIndex = ~0u;
        float currentTime = 0.0f;
        bool isLooping = true;
        bool isPlaying = false;

        // For smooth transitions (future proofing)
        float weight = 1.0f;
    };

    // --- Skeleton Structures ---

    struct MorphTargetInfo
    {
        uint32_t meshIndex = ~0u;
        std::vector<uint32_t> targetOffsets; // Offsets into global delta buffer
    };

    struct MorphStateGPU
    {
        uint32_t meshIndex = ~0u;
        uint32_t activeTargets[8] = {0};
        float weights[8] = {0.0f};
    };

    class ModelDOD
    {
    public:
        ModelDOD();
        ~ModelDOD();

        static std::unique_ptr<ModelDOD> load(RHIRenderer& renderer, const std::filesystem::path& path,
                                              bool vertexPulling = false);

        bool saveCache(const std::filesystem::path& path);
        bool loadCache(const std::filesystem::path& path, RHIRenderer& renderer);

        // Assets
        const std::vector<MaterialData>& materials() const { return m_materials; }
        const std::vector<TextureHandle>& textures() const { return m_textures; }
        const std::vector<MeshDOD>& meshes() const { return m_meshes; }
        const std::vector<BoundingBox>& meshBounds() const { return m_meshBounds; }
        const std::vector<Skin>& skins() const { return m_skins; }
        const std::vector<Animation>& animations() const { return m_animations; }
        const std::vector<GltfCamera>& cameras() const { return m_cameras; }

        std::vector<MorphTargetInfo>& morphTargetInfos() { return m_morphTargetInfos; }
        const std::vector<MorphTargetInfo>& morphTargetInfos() const { return m_morphTargetInfos; }
        std::vector<MorphStateGPU>& morphStates() { return m_morphStates; }
        const std::vector<MorphStateGPU>& morphStates() const { return m_morphStates; }

        AnimationState& animationState() { return m_animState; }
        const AnimationState& animationState() const { return m_animState; }

        std::vector<MaterialData>& materialsMutable() { return m_materials; }
        std::vector<TextureHandle>& texturesMutable() { return m_textures; }
        std::vector<MeshDOD>& meshesMutable() { return m_meshes; }

        uint32_t addPrimitiveToScene(RHIRenderer& renderer,
                                     const geometry::MeshData& primitiveData,
                                     uint32_t materialIndex = 0,
                                     const glm::mat4& transform = glm::mat4(1.0f),
                                     const std::string& name = "Primitive");
        void addPrimitiveMeshes(RHIRenderer& renderer,
                                const std::vector<geometry::MeshData>& primitives,
                                const std::vector<std::string>& names,
                                uint32_t materialIndex = 0);
        void dropCpuGeometry();

        int32_t addLight(const Light& light, const glm::mat4& transform = glm::mat4(1.0f), const std::string& name = "Light");
        void removeLight(int32_t lightIndex);

        const std::vector<MaterialCPU>& materialsCPU() const { return m_materialsCPU; }
        const std::vector<std::string>& textureFiles() const { return m_textureFiles; }

        // The DOD Scene
        SceneGraphDOD& scene() { return *m_scene; }
        const SceneGraphDOD& scene() const { return *m_scene; }

        // Unified GPU Buffers for geometry
        BufferHandle vertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle morphVertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle morphStateBuffer = INVALID_BUFFER_HANDLE;

        friend class ModelDODLoader; // Allowing loader access to private members for loading

    private:
        uint32_t appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                         uint32_t materialIndex,
                                         const std::string& name);
        void uploadUnifiedBuffers(RHIRenderer& renderer);
        std::vector<TextureHandle> m_textures;
        std::vector<MaterialData> m_materials;
        std::vector<MeshDOD> m_meshes;
        std::vector<Skin> m_skins;
        std::vector<Animation> m_animations;
        std::vector<GltfCamera> m_cameras;
        std::vector<MorphTargetInfo> m_morphTargetInfos;
        std::vector<MorphStateGPU> m_morphStates;
        AnimationState m_animState;

        std::vector<MaterialCPU> m_materialsCPU;
        std::vector<std::string> m_textureFiles;
        std::vector<uint8_t> m_textureIsSrgb;

        std::unique_ptr<SceneGraphDOD> m_scene;
        std::vector<BoundingBox> m_meshBounds;
        std::vector<Vertex> m_cpuVertices;
        std::vector<uint32_t> m_cpuIndices;
    };
} // namespace pnkr::renderer::scene
