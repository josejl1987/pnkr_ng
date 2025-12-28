#include "pnkr/renderer/scene/VtxData.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include "pnkr/core/common.hpp"
#include <cstdio>
#include <cpptrace/cpptrace.hpp>
#include <unordered_map>
#include <algorithm>
#include <limits>

namespace pnkr::renderer::scene
{
    static constexpr uint32_t kMagic = 0x12345678;

    bool loadUnifiedMeshData(const char* meshFile, UnifiedMeshData& out)
    {
        FILE* f = nullptr;
        // Check for fopen_s availability or use standard fopen
#ifdef _MSC_VER
        fopen_s(&f, meshFile, "rb");
#else
        f = fopen(meshFile, "rb");
#endif
        if (!f) return false;
        auto guard = util::makeScopeGuard([&] { fclose(f); });

        MeshFileHeader header{}; // value-init
        if (fread(&header, 1, sizeof(header), f) != sizeof(header)) return false;
        if (header.m_magicValue != kMagic) return false;
        if ((header.m_indexDataSize % sizeof(uint32_t)) != 0) return false;

        out = {}; // avoid partial outputs on failure

        out.m_meshes.resize(header.m_meshCount);
        out.m_boxes.resize(header.m_meshCount);

        if (header.m_meshCount > 0)
        {
            if (fread(out.m_meshes.data(), sizeof(UnifiedMesh), header.m_meshCount, f) != header.m_meshCount) return false;
            if (fread(out.m_boxes.data(),  sizeof(BoundingBox), header.m_meshCount, f) != header.m_meshCount) return false;
        }

        if (header.m_indexDataSize > 0)
        {
            const size_t indexCount = header.m_indexDataSize / sizeof(uint32_t);
            out.m_indexData.resize(indexCount);
            if (fread(out.m_indexData.data(), 1, header.m_indexDataSize, f) != header.m_indexDataSize) return false;
        }

        if (header.m_vertexDataSize > 0)
        {
            out.m_vertexData.resize(header.m_vertexDataSize);
            if (fread(out.m_vertexData.data(), 1, header.m_vertexDataSize, f) != header.m_vertexDataSize) return false;
        }

        return true;
    }

    void saveUnifiedMeshData(const char* filename, const UnifiedMeshData& data)
    {
        if (data.m_boxes.size() != data.m_meshes.size())
            throw cpptrace::runtime_error("UnifiedMeshData invariant failed: m_boxes.size() must equal m_meshes.size()");

        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, filename, "wb");
#else
        f = fopen(filename, "wb");
#endif
        if (!f) throw cpptrace::runtime_error("Failed to open file for writing");
        auto guard = util::makeScopeGuard([&] { fclose(f); });

        MeshFileHeader header{};
        header.m_magicValue     = kMagic;
        header.m_meshCount      = static_cast<uint32_t>(data.m_meshes.size());
        header.m_indexDataSize  = static_cast<uint32_t>(data.m_indexData.size() * sizeof(uint32_t));
        header.m_vertexDataSize = static_cast<uint32_t>(data.m_vertexData.size());

        if (fwrite(&header, 1, sizeof(header), f) != sizeof(header))
            throw cpptrace::runtime_error("Write failed (header)");

        if (header.m_meshCount > 0)
        {
            if (fwrite(data.m_meshes.data(), sizeof(UnifiedMesh), header.m_meshCount, f) != header.m_meshCount)
                throw cpptrace::runtime_error("Write failed (meshes)");
            if (fwrite(data.m_boxes.data(), sizeof(BoundingBox), header.m_meshCount, f) != header.m_meshCount)
                throw cpptrace::runtime_error("Write failed (boxes)");
        }

        if (header.m_indexDataSize > 0)
        {
            if (fwrite(data.m_indexData.data(), 1, header.m_indexDataSize, f) != header.m_indexDataSize)
                throw cpptrace::runtime_error("Write failed (index data)");
        }

        if (header.m_vertexDataSize > 0)
        {
            if (fwrite(data.m_vertexData.data(), 1, header.m_vertexDataSize, f) != header.m_vertexDataSize)
                throw cpptrace::runtime_error("Write failed (vertex data)");
        }
    }

    static void mergeIndexArray(UnifiedMeshData& md, 
                                const std::vector<uint32_t>& meshesToMerge, 
                                std::unordered_map<uint32_t, uint32_t>& oldToNew)
    {
        std::vector<uint32_t> newIndices(md.m_indexData.size());
        uint32_t copyOffset = 0;
        
        // Shift indices in place first to align vertices relative to the new base
        // Note: shiftMeshIndices returns where the merged block *would* start if we were appending
        // but here we are partitioning.
        // Actually, we need to be careful not to double-shift if we run this multiple times.
        // For this specific recipe, we assume a fresh merge pass.
        
        // Calculate offsets
        uint32_t mergeCount = 0;
        for(uint32_t m : meshesToMerge) mergeCount += md.m_meshes[m].getLODIndicesCount(0);
        uint32_t mergeOffset = (uint32_t)md.m_indexData.size() - mergeCount;

        // Perform the vertex offset fixup (shiftMeshIndices logic inlined/adapted for safety)
        uint32_t minVtxOffset = std::numeric_limits<uint32_t>::max();
        for (uint32_t i : meshesToMerge) minVtxOffset = std::min(md.m_meshes[i].vertexOffset, minVtxOffset);
        
        // The merged mesh index will be the last one after compaction
        const uint32_t mergedMeshIndex = (uint32_t)(md.m_meshes.size() - meshesToMerge.size());
        uint32_t newIndexCounter = 0;

        // Sort meshesToMerge for binary search
        std::vector<uint32_t> sortedMerge = meshesToMerge;
        std::sort(sortedMerge.begin(), sortedMerge.end());

        // Rebuild Mesh List
        std::vector<UnifiedMesh> newMeshes;
        newMeshes.reserve(mergedMeshIndex + 1);

        for (uint32_t midx = 0; midx < md.m_meshes.size(); ++midx) {
            UnifiedMesh& mesh = md.m_meshes[midx];
            bool shouldMerge = std::binary_search(sortedMerge.begin(), sortedMerge.end(), midx);

            if (shouldMerge) {
                oldToNew[midx] = mergedMeshIndex;
                
                // Fix vertex indices
                const uint32_t delta = mesh.vertexOffset - minVtxOffset;
                const uint32_t idxCount = mesh.getLODIndicesCount(0);
                const auto start = md.m_indexData.begin() + mesh.indexOffset;
                
                // Copy to the end of the new buffer
                auto dest = newIndices.begin() + mergeOffset;
                for (size_t k = 0; k < idxCount; ++k) {
                    *(dest + k) = *(start + k) + delta;
                }
                
                mergeOffset += idxCount;
            } else {
                oldToNew[midx] = newIndexCounter++;
                
                const uint32_t idxCount = mesh.getLODIndicesCount(0);
                const auto start = md.m_indexData.begin() + mesh.indexOffset;
                
                // Copy to start
                std::copy(start, start + idxCount, newIndices.begin() + copyOffset);
                
                mesh.indexOffset = copyOffset;
                mesh.m_lodOffset[0] = copyOffset;
                mesh.m_lodOffset[1] = copyOffset + idxCount; // Assuming 1 LOD for simplification
                copyOffset += idxCount;
                
                newMeshes.push_back(mesh);
            }
        }

        // Add the single merged mesh
        if (!meshesToMerge.empty()) {
            UnifiedMesh mergedMesh = md.m_meshes[meshesToMerge[0]]; // Copy properties from first
            mergedMesh.vertexOffset = minVtxOffset;
            mergedMesh.indexOffset = (uint32_t)md.m_indexData.size() - mergeCount; // Start of merged block
            mergedMesh.lodCount = 1;
            mergedMesh.m_lodOffset[0] = mergedMesh.indexOffset;
            mergedMesh.m_lodOffset[1] = (uint32_t)md.m_indexData.size();
            
            newMeshes.push_back(mergedMesh);
        }

        md.m_indexData = newIndices;
        md.m_meshes = newMeshes;
    }

    void mergeNodesWithMaterial(SceneGraphDOD& scene, UnifiedMeshData& meshData, uint32_t materialID)
    {
        std::vector<ecs::Entity> entitiesToDelete;
        std::vector<uint32_t> meshesToMerge;

        // 1. Find nodes using this material
        auto meshPool = scene.registry.view<MeshRenderer>();
        meshPool.each([&](ecs::Entity e, MeshRenderer& mr) {
            int32_t midx = mr.meshID;
            if (midx >= 0 && (size_t)midx < meshData.m_meshes.size()) {
                if (meshData.m_meshes[midx].materialID == materialID) {
                    entitiesToDelete.push_back(e);
                    meshesToMerge.push_back((uint32_t)midx);
                }
            }
        });

        if (meshesToMerge.size() < 2) return; // Nothing to merge

        // 2. Merge Mesh Data (Indices and Vertex Offsets)
        std::vector<BoundingBox> oldBoxes = meshData.m_boxes;
        std::unordered_map<uint32_t, uint32_t> oldToNewMeshID;
        mergeIndexArray(meshData, meshesToMerge, oldToNewMeshID);

        // 3. Update Scene Mesh References
        meshPool.each([&](ecs::Entity, MeshRenderer& mr) {
            int32_t mIdx = mr.meshID;
            if (mIdx >= 0) {
                // If it was in the map (merged or shifted), update it
                if (oldToNewMeshID.find((uint32_t)mIdx) != oldToNewMeshID.end()) {
                    mr.meshID = (int32_t)oldToNewMeshID[(uint32_t)mIdx];
                }
            }
        });

        if (!oldBoxes.empty()) {
            std::vector<BoundingBox> newBoxes(meshData.m_meshes.size());
            std::vector<uint8_t> boxInit(meshData.m_meshes.size(), 0);
            for (size_t i = 0; i < oldBoxes.size(); ++i) {
                auto it = oldToNewMeshID.find(static_cast<uint32_t>(i));
                if (it == oldToNewMeshID.end()) continue;
                const uint32_t newIdx = it->second;
                if (newIdx >= newBoxes.size()) continue;

                if (!boxInit[newIdx]) {
                    newBoxes[newIdx] = oldBoxes[i];
                    boxInit[newIdx] = 1;
                } else {
                    newBoxes[newIdx].m_min = glm::min(newBoxes[newIdx].m_min, oldBoxes[i].m_min);
                    newBoxes[newIdx].m_max = glm::max(newBoxes[newIdx].m_max, oldBoxes[i].m_max);
                }
            }
            meshData.m_boxes = std::move(newBoxes);
        }

        // 4. Create New Node for the Merged Mesh
        ecs::Entity newNode = scene.createNode();
        const int32_t mergedMeshId = (int32_t)meshData.m_meshes.size() - 1;
        scene.registry.emplace<MeshRenderer>(newNode, mergedMeshId);
        LocalBounds& lb = scene.registry.emplace<LocalBounds>(newNode);
        if (mergedMeshId >= 0 && static_cast<size_t>(mergedMeshId) < meshData.m_boxes.size()) {
            lb.aabb = meshData.m_boxes[mergedMeshId];
        }
        scene.registry.emplace<WorldBounds>(newNode);
        scene.registry.emplace<Visibility>(newNode);
        scene.registry.emplace<BoundsDirtyTag>(newNode);

        // 5. Delete the old nodes
        for (ecs::Entity e : entitiesToDelete) {
            scene.destroyNode(e);
        }
    }
}
