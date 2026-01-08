#pragma once
#include "pnkr/core/ECS.hpp"
#include "pnkr/renderer/scene/Components.hpp"
#include <vector>

namespace pnkr::renderer::scene {

    class SceneGraphDOD {
    public:
        ecs::Registry& registry() noexcept { return registry_; }
        const ecs::Registry& registry() const noexcept { return registry_; }

        ecs::Entity createNode(ecs::Entity parent = ecs::NULL_ENTITY);
        void destroyNode(ecs::Entity entity);

        std::vector<ecs::Entity>& topoOrder() noexcept { return topoOrder_; }
        const std::vector<ecs::Entity>& topoOrder() const noexcept { return topoOrder_; }

        std::vector<ecs::Entity>& roots() noexcept { return roots_; }
        const std::vector<ecs::Entity>& roots() const noexcept { return roots_; }

        bool hierarchyDirty() const noexcept { return hierarchyDirty_; }
        void setHierarchyDirty(bool dirty) noexcept { hierarchyDirty_ = dirty; }
        void onHierarchyChanged();

        void recalculateGlobalTransformsFull();

        void recalculateGlobalTransformsDirty() { updateTransforms(); }

        void updateTransforms();

        void markAsChanged(ecs::Entity entity);

        void setParent(ecs::Entity entity, ecs::Entity parent);

        ecs::Entity root() const noexcept { return root_; }

        uint32_t materialBaseIndex() const noexcept { return materialBaseIndex_; }
        void setMaterialBaseIndex(uint32_t baseIndex) noexcept { materialBaseIndex_ = baseIndex; }

    private:
        void updateTopoOrder();

        ecs::Registry registry_;
        std::vector<ecs::Entity> topoOrder_;
        std::vector<ecs::Entity> roots_;
        bool hierarchyDirty_ = false;
        ecs::Entity root_ = ecs::NULL_ENTITY;
        uint32_t materialBaseIndex_ = 0;
    };
}
