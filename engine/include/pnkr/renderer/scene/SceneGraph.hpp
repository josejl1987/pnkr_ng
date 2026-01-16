#pragma once
#include "pnkr/core/ECS.hpp"
#include "pnkr/renderer/scene/Components.hpp"
#include <vector>

namespace pnkr::renderer::scene {

    class SceneGraphDOD {
    public:
        ecs::Registry& registry() noexcept { return m_registry; }
        const ecs::Registry& registry() const noexcept { return m_registry; }

        ecs::Entity createNode(ecs::Entity parent = ecs::kNullEntity);
        void destroyNode(ecs::Entity entity);

        std::vector<ecs::Entity>& topoOrder() noexcept { return m_topoOrder; }
        const std::vector<ecs::Entity>& topoOrder() const noexcept { return m_topoOrder; }

        std::vector<ecs::Entity>& roots() noexcept { return m_roots; }
        const std::vector<ecs::Entity>& roots() const noexcept { return m_roots; }

        bool hierarchyDirty() const noexcept { return m_hierarchyDirty; }
        void setHierarchyDirty(bool dirty) noexcept { m_hierarchyDirty = dirty; }
        void onHierarchyChanged();

        void recalculateGlobalTransformsFull();

        void recalculateGlobalTransformsDirty() { updateTransforms(); }

        void updateTransforms();

        void markAsChanged(ecs::Entity entity);

        void setParent(ecs::Entity entity, ecs::Entity parent);

        ecs::Entity root() const noexcept { return m_root; }
        void setRoot(ecs::Entity r) noexcept { m_root = r; }

        uint32_t materialBaseIndex() const noexcept { return m_materialBaseIndex; }
        void setMaterialBaseIndex(uint32_t baseIndex) noexcept { m_materialBaseIndex = baseIndex; }

    private:
        void updateTopoOrder();

        ecs::Registry m_registry;
        std::vector<ecs::Entity> m_topoOrder;
        std::vector<ecs::Entity> m_roots;
        bool m_hierarchyDirty = false;
        ecs::Entity m_root = ecs::kNullEntity;
        uint32_t m_materialBaseIndex = 0;
    };
}
