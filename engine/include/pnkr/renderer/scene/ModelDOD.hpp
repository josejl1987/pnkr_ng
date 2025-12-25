#pragma once

#include "pnkr/renderer/scene/Model.hpp" // Reuse MaterialData/Light definitions
#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/VtxData.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include <filesystem>
#include <vector>

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

    class ModelDOD
    {
    public:
        static std::unique_ptr<ModelDOD> load(RHIRenderer& renderer, const std::filesystem::path& path,
                                              bool vertexPulling = false);

        bool saveCache(const std::filesystem::path& path);
        bool loadCache(const std::filesystem::path& path, RHIRenderer& renderer);

        // Assets
        const std::vector<MaterialData>& materials() const { return m_materials; }
        const std::vector<Light>& lights() const { return m_lights; }
        const std::vector<TextureHandle>& textures() const { return m_textures; }
        const std::vector<MeshDOD>& meshes() const { return m_meshes; }
        const std::vector<BoundingBox>& meshBounds() const { return m_meshBounds; }
        std::vector<MaterialData>& materialsMutable() { return m_materials; }
        std::vector<Light>& lightsMutable() { return m_lights; }
        std::vector<TextureHandle>& texturesMutable() { return m_textures; }
        std::vector<MeshDOD>& meshesMutable() { return m_meshes; }

        const std::vector<MaterialCPU>& materialsCPU() const { return m_materialsCPU; }
        const std::vector<std::string>& textureFiles() const { return m_textureFiles; }

        // The DOD Scene
        SceneGraphDOD& scene() { return m_scene; }
        const SceneGraphDOD& scene() const { return m_scene; }

        // Unified GPU Buffers for geometry
        BufferHandle vertexBuffer = INVALID_BUFFER_HANDLE;
        BufferHandle indexBuffer = INVALID_BUFFER_HANDLE;

    private:
        std::vector<TextureHandle> m_textures;
        std::vector<MaterialData> m_materials;
        std::vector<Light> m_lights;
        std::vector<MeshDOD> m_meshes;

        std::vector<MaterialCPU> m_materialsCPU;
        std::vector<std::string> m_textureFiles;
        std::vector<uint8_t> m_textureIsSrgb;

        SceneGraphDOD m_scene;
        std::vector<BoundingBox> m_meshBounds;
    };
} // namespace pnkr::renderer::scene
