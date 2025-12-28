#pragma once
#include "pnkr/core/ECS.hpp"
#include "pnkr/renderer/scene/Components.hpp"
#include <vector>

namespace pnkr::renderer::scene {

    class SceneGraphDOD {
    public:
        ecs::Registry registry;
        
        // Helper wrappers
        ecs::Entity createNode(ecs::Entity parent = ecs::NULL_ENTITY);
        void destroyNode(ecs::Entity entity);

        // Keep track of topological order for transform updates
        std::vector<ecs::Entity> topoOrder;
        
        // Root entities (those with no parent)
        std::vector<ecs::Entity> roots;

        bool hierarchyDirty = false;
        void onHierarchyChanged();
        
        // Full update (topoOrder-based).
        void recalculateGlobalTransformsFull();
        
        // Compatibility alias for old scene graph
        void recalculateGlobalTransformsDirty() { updateTransforms(); }
        
        // Updates only what's needed or full depending on implementation
        void updateTransforms();
        
        void markAsChanged(ecs::Entity entity);

        void setParent(ecs::Entity entity, ecs::Entity parent);

        // Synthetic root node id (compatibility)
        ecs::Entity root = ecs::NULL_ENTITY;

    private:
        void updateTopoOrder();
    };
}