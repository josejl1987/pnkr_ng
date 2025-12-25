#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

#include "SceneGraph.hpp" // Required for SceneGraphDOD

namespace pnkr::renderer::scene {

    constexpr const uint32_t kMaxLODs = 8;

    // Mesh descriptor stored in the file
    struct UnifiedMesh {
        uint32_t lodCount = 1;
        uint32_t indexOffset = 0;
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t materialID = 0;
        uint32_t m_lodOffset[kMaxLODs + 1] = { 0 };

        [[nodiscard]] uint32_t getLODIndicesCount(uint32_t lod) const {
            return lod < lodCount ? m_lodOffset[lod + 1] - m_lodOffset[lod] : 0;
        }
    };

    struct MeshFileHeader {
        uint32_t m_magicValue = 0x12345678;
        uint32_t m_meshCount = 0;
        uint32_t m_indexDataSize = 0; // Bytes
        uint32_t m_vertexDataSize = 0; // Bytes
    };

    struct BoundingBox {
        glm::vec3 m_min;
        glm::vec3 m_max;
    };

    // CPU-side container
    struct UnifiedMeshData {
        std::vector<uint32_t> m_indexData;
        std::vector<uint8_t> m_vertexData;
        std::vector<UnifiedMesh> m_meshes;
        std::vector<BoundingBox> m_boxes;
    };

    // I/O Functions
    bool loadUnifiedMeshData(const char* meshFile, UnifiedMeshData& out);
    void saveUnifiedMeshData(const char* filename, const UnifiedMeshData& data);

    // Merges meshes that share a material and removes their individual nodes,
    // creating a single new node for the combined mesh.
    // Modifies both the SceneGraph and the MeshData.
    void mergeNodesWithMaterial(SceneGraphDOD& scene, UnifiedMeshData& meshData, uint32_t materialID);

} // namespace pnkr::renderer::scene