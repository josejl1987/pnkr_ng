#pragma once

#include <vector>
#include <string>
#include <glm/mat4x4.hpp>
#include <memory>
#include <cstdint>
#include <fastgltf/types.hpp>

namespace pnkr::renderer::scene {

    // The compact hierarchy node (Left-Child / Right-Sibling)
    struct HierarchyDOD {
        int32_t parent      = -1;
        int32_t firstChild  = -1;
        int32_t nextSibling = -1;
        int32_t lastChild   = -1;
        uint16_t level      = 0;
    };

    struct SceneGraphDOD {
        // SoA transforms
        std::vector<glm::mat4> local;
        std::vector<glm::mat4> global;

        // Hierarchy
        std::vector<HierarchyDOD> hierarchy;

        // Dense optional components (hot path friendly)
        std::vector<int32_t> meshIndex;   // -1 if none (gltf node meshIndex)
        std::vector<int32_t> lightIndex;  // -1 if none (KHR_lights_punctual)
        std::vector<int32_t> nameId;      // -1 if none (index into names)

        std::vector<std::string> names;

        // Roots for current glTF scene (node indices mapped to our node ids)
        std::vector<uint32_t> roots;

        // Parent-before-child order for fast global update
        std::vector<uint32_t> topoOrder;

        // Synthetic root node id (optional but recommended)
        uint32_t root = 0;

        // ---------------------------------------------------------------------
        // TransformTree dirty tracking (per-frame)
        // ---------------------------------------------------------------------
        std::vector<std::vector<uint32_t>> changedAtThisFrame; // indexed by level
        std::vector<uint8_t> dirtyFlag;                        // 0/1 per node
        std::vector<uint32_t> dirtyNodes;                      // nodes marked this frame (for fast reset)
        uint16_t maxLevel = 0;                                 // max depth in this scene
        uint16_t maxDirtyLevelThisFrame = 0;                   // max level touched this frame

        void clear() {
            local.clear(); global.clear(); hierarchy.clear();
            meshIndex.clear(); lightIndex.clear(); nameId.clear();
            names.clear(); roots.clear(); topoOrder.clear();
            root = 0;

            changedAtThisFrame.clear();
            dirtyFlag.clear();
            dirtyNodes.clear();
            maxLevel = 0;
            maxDirtyLevelThisFrame = 0;
        }

        void appendChild(uint32_t parent, uint32_t child);
        void buildFromFastgltf(const fastgltf::Asset& gltf, size_t sceneIndex);

        // Full update (topoOrder-based). Useful after load or for debugging/fallback.
        void recalculateGlobalTransformsFull();

        // Dirty-tracked update
        void initDirtyTracking();
        void beginFrameDirty();               // optional: call once per frame
        void markAsChanged(uint32_t node);    // marks node + descendants
        void recalculateGlobalTransformsDirty();

        // Hierarchy management
        bool hierarchyDirty = false;
        void onHierarchyChanged();
    };

} // namespace pnkr::renderer::scene
