#pragma once

#include "pnkr/renderer/material/Material.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/Animation.hpp"
#include "pnkr/renderer/scene/GltfCamera.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/geometry/GeometryUtils.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/assets/ImportedData.hpp"
#include <filesystem>
#include <vector>
#include <span>
#include <cstddef>
#include <memory>
#include <limits>

#include "pnkr/renderer/gpu_shared/SkinningShared.h"
#include "pnkr/renderer/scene/SceneAssetDatabase.hpp"
#include "pnkr/renderer/scene/SceneState.hpp"

namespace pnkr::renderer::scene
{
    class ModelDOD
    {
    public:
        ModelDOD() : m_scene(std::make_unique<SceneGraphDOD>()) {}
        ~ModelDOD() = default;

        const std::vector<MaterialData>& materials() const { return m_assets.materials(); }
        const std::vector<TexturePtr>& textures() const { return m_assets.textures(); }
        const std::vector<TexturePtr>& pendingTextures() const { return m_assets.pendingTextures(); }
        const std::vector<MeshDOD>& meshes() const { return m_assets.meshes(); }
        const std::vector<BoundingBox>& meshBounds() const { return m_assets.meshBounds(); }
        const std::vector<Skin>& skins() const { return m_assets.skins(); }
        const std::vector<Animation>& animations() const { return m_assets.animations(); }
        std::vector<Skin>& skinsMutable() { return m_assets.skinsMutable(); }
        std::vector<Animation>& animationsMutable() { return m_assets.animationsMutable(); }
        const std::vector<GltfCamera>& cameras() const { return m_assets.cameras(); }
        std::vector<GltfCamera>& camerasMutable() { return m_assets.camerasMutable(); }

        std::vector<MorphTargetInfo>& morphTargetInfos() { return m_assets.morphTargetInfosMutable(); }
        const std::vector<MorphTargetInfo>& morphTargetInfos() const { return m_assets.morphTargetInfos(); }
        std::vector<gpu::MorphState>& morphStates() { return m_state.morphStates(); }
        const std::vector<gpu::MorphState>& morphStates() const { return m_state.morphStates(); }

        AnimationState& animationState() { return m_state.animationState(); }
        const AnimationState& animationState() const { return m_state.animationState(); }

        std::vector<MaterialData>& materialsMutable() { return m_assets.materialsMutable(); }
        std::vector<TexturePtr>& texturesMutable() { return m_assets.texturesMutable(); }
        std::vector<TexturePtr>& pendingTexturesMutable() { return m_assets.pendingTexturesMutable(); }
        std::vector<MeshDOD>& meshesMutable() { return m_assets.meshesMutable(); }
        std::vector<BoundingBox>& meshBoundsMutable() { return m_assets.meshBoundsMutable(); }
        std::vector<MaterialCPU>& materialsCPUMutable() { return m_assets.materialsCPUMutable(); }
        std::vector<Vertex>& cpuVerticesMutable() { return m_assets.cpuVerticesMutable(); }
        std::vector<uint32_t>& cpuIndicesMutable() { return m_assets.cpuIndicesMutable(); }
        std::vector<std::string>& textureFilesMutable() { return m_assets.textureFilesMutable(); }
        const std::vector<uint8_t>& textureIsSrgb() const { return m_assets.textureIsSrgb(); }
        std::vector<uint8_t>& textureIsSrgbMutable() { return m_assets.textureIsSrgbMutable(); }

        uint32_t appendPrimitiveMeshData(const geometry::MeshData& primitiveData,
                                                   uint32_t materialIndex,
                                                   const std::string& name);

        void uploadUnifiedBuffers(RHIRenderer& renderer);

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

        const std::vector<MaterialCPU>& materialsCPU() const { return m_assets.materialsCPU(); }
        const std::vector<std::string>& textureFiles() const { return m_assets.textureFiles(); }

        SceneGraphDOD& scene() { return *m_scene; }
        const SceneGraphDOD& scene() const { return *m_scene; }

        // Buffer accessors
        // Exposed via getters for compatibility and encapsulation.

        SceneAssetDatabase& assets() { return m_assets; }
        const SceneAssetDatabase& assets() const { return m_assets; }

        SceneState& state() { return m_state; }
        const SceneState& state() const { return m_state; }

        // Wrapper getters for buffers
        BufferPtr vertexBuffer() const { return m_assets.vertexBuffer; }
        BufferPtr indexBuffer() const { return m_assets.indexBuffer; }
        BufferPtr boundsBuffer() const { return m_assets.boundsBuffer; }
        BufferPtr visibleListBuffer() const { return m_visibleListBuffer; }
        BufferPtr morphVertexBuffer() const { return m_state.morphVertexBuffer; }
        BufferPtr morphStateBuffer() const { return m_state.morphStateBuffer; }

        // Setters for buffers if needed (or just access via assets()/state())
        void setMorphVertexBuffer(BufferPtr buffer) { m_state.morphVertexBuffer = buffer; }
        
        // Manually managed buffer for visible instances
        void setVisibleListBuffer(BufferPtr buffer) { m_visibleListBuffer = buffer; }

    private:
        SceneAssetDatabase m_assets;
        SceneState m_state;
        BufferPtr m_visibleListBuffer{};

        std::unique_ptr<SceneGraphDOD> m_scene;
    };
}
