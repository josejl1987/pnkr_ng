#include "pnkr/renderer/scene/SceneGraph.hpp"
#include <algorithm>
#include <stack>

namespace pnkr::renderer::scene {

    ecs::Entity SceneGraphDOD::createNode(ecs::Entity parent) {
        ecs::Entity entity = registry.create();
        if (root == ecs::NULL_ENTITY) root = entity; // Compatibility: first node is root

        registry.emplace<LocalTransform>(entity);
        registry.emplace<WorldTransform>(entity);
        Relationship& rel = registry.emplace<Relationship>(entity);
        
        if (parent != ecs::NULL_ENTITY) {
            rel.parent = parent;
            Relationship& parentRel = registry.get<Relationship>(parent);
            rel.level = parentRel.level + 1;
            
            if (parentRel.firstChild == ecs::NULL_ENTITY) {
                parentRel.firstChild = entity;
                parentRel.lastChild = entity;
            } else {
                ecs::Entity lastChild = parentRel.lastChild;
                registry.get<Relationship>(lastChild).nextSibling = entity;
                rel.prevSibling = lastChild;
                parentRel.lastChild = entity;
            }
        } else {
            roots.push_back(entity);
            rel.level = 0;
        }
        
        hierarchyDirty = true;
        return entity;
    }

    void SceneGraphDOD::destroyNode(ecs::Entity entity) {
        if (!registry.has<Relationship>(entity)) return;

        Relationship& rel = registry.get<Relationship>(entity);
        
        // Recursively destroy children
        ecs::Entity child = rel.firstChild;
        while (child != ecs::NULL_ENTITY) {
            ecs::Entity next = registry.get<Relationship>(child).nextSibling;
            destroyNode(child);
            child = next;
        }
        
        // Remove from parent's list
        if (rel.parent != ecs::NULL_ENTITY) {
            Relationship& parentRel = registry.get<Relationship>(rel.parent);
            if (parentRel.firstChild == entity) {
                parentRel.firstChild = rel.nextSibling;
            }
            if (parentRel.lastChild == entity) {
                parentRel.lastChild = rel.prevSibling;
            }
        } else {
            // Remove from roots
            auto it = std::find(roots.begin(), roots.end(), entity);
            if (it != roots.end()) roots.erase(it);
        }
        
        // Fix siblings
        if (rel.prevSibling != ecs::NULL_ENTITY) {
            registry.get<Relationship>(rel.prevSibling).nextSibling = rel.nextSibling;
        }
        if (rel.nextSibling != ecs::NULL_ENTITY) {
            registry.get<Relationship>(rel.nextSibling).prevSibling = rel.prevSibling;
        }
        
        if (root == entity) root = ecs::NULL_ENTITY;

        registry.destroy(entity);
        hierarchyDirty = true;
    }

    void SceneGraphDOD::updateTopoOrder() {
        topoOrder.clear();
        std::stack<ecs::Entity> stack;
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            stack.push(*it);
        }

        while (!stack.empty()) {
            ecs::Entity e = stack.top();
            stack.pop();
            topoOrder.push_back(e);
            
            Relationship& rel = registry.get<Relationship>(e);
            
            ecs::Entity child = rel.firstChild;
            std::vector<ecs::Entity> children;
            while (child != ecs::NULL_ENTITY) {
                children.push_back(child);
                child = registry.get<Relationship>(child).nextSibling;
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push(*it);
            }
        }
        hierarchyDirty = false;
    }

    void SceneGraphDOD::recalculateGlobalTransformsFull() {
        if (hierarchyDirty) updateTopoOrder();
        
        for (ecs::Entity e : topoOrder) {
            const Relationship& rel = registry.get<Relationship>(e);
            const glm::mat4& local = registry.get<LocalTransform>(e).matrix;
            
            if (rel.parent != ecs::NULL_ENTITY) {
                registry.get<WorldTransform>(e).matrix = registry.get<WorldTransform>(rel.parent).matrix * local;
            } else {
                registry.get<WorldTransform>(e).matrix = local;
            }
            registry.remove<TransformDirtyTag>(e);
        }
    }

    void SceneGraphDOD::updateTransforms() {
        if (hierarchyDirty) {
            recalculateGlobalTransformsFull();
            return;
        }

        // 1. Identify if anything is dirty
        if (registry.getPool<TransformDirtyTag>().size() == 0) return;

        for (ecs::Entity e : topoOrder) {
            Relationship& rel = registry.get<Relationship>(e);
            bool isDirty = registry.has<TransformDirtyTag>(e);
            
            if (!isDirty && rel.parent != ecs::NULL_ENTITY) {
                if (registry.has<TransformDirtyTag>(rel.parent)) {
                    registry.emplace<TransformDirtyTag>(e);
                    isDirty = true;
                }
            }

            if (isDirty) {
                const glm::mat4& local = registry.get<LocalTransform>(e).matrix;
                if (rel.parent != ecs::NULL_ENTITY) {
                    registry.get<WorldTransform>(e).matrix = registry.get<WorldTransform>(rel.parent).matrix * local;
                } else {
                    registry.get<WorldTransform>(e).matrix = local;
                }
            }
        }

        // 2. Clear all dirty tags
        registry.getPool<TransformDirtyTag>().clear();
    }

    void SceneGraphDOD::markAsChanged(ecs::Entity entity) {
        if (!registry.has<TransformDirtyTag>(entity)) {
            registry.emplace<TransformDirtyTag>(entity);
        }
    }

    void SceneGraphDOD::onHierarchyChanged() {
        hierarchyDirty = true;
        updateTopoOrder();
    }

    void SceneGraphDOD::setParent(ecs::Entity entity, ecs::Entity parent) {
        if (entity == parent) return;
        if (parent != ecs::NULL_ENTITY && registry.getPool<Relationship>().has(parent)) {
            ecs::Entity current = parent;
            while (current != ecs::NULL_ENTITY) {
                if (current == entity) return;
                Relationship& rel = registry.get<Relationship>(current);
                current = rel.parent;
            }
        }
        Relationship& rel = registry.getPool<Relationship>().has(entity) ? 
            registry.get<Relationship>(entity) : 
            registry.emplace<Relationship>(entity);
            
        // Remove from old parent if any
        if (rel.parent != ecs::NULL_ENTITY) {
            Relationship& oldParentRel = registry.get<Relationship>(rel.parent);
            if (oldParentRel.firstChild == entity) oldParentRel.firstChild = rel.nextSibling;
            if (oldParentRel.lastChild == entity) oldParentRel.lastChild = rel.prevSibling;
            
            if (rel.prevSibling != ecs::NULL_ENTITY) registry.get<Relationship>(rel.prevSibling).nextSibling = rel.nextSibling;
            if (rel.nextSibling != ecs::NULL_ENTITY) registry.get<Relationship>(rel.nextSibling).prevSibling = rel.prevSibling;
        } else {
            auto it = std::find(roots.begin(), roots.end(), entity);
            if (it != roots.end()) roots.erase(it);
        }

        rel.parent = parent;
        if (parent != ecs::NULL_ENTITY) {
            Relationship& parentRel = registry.getPool<Relationship>().has(parent) ? 
                registry.get<Relationship>(parent) : 
                registry.emplace<Relationship>(parent);
            
            rel.level = parentRel.level + 1;
            rel.nextSibling = ecs::NULL_ENTITY;
            rel.prevSibling = parentRel.lastChild;

            if (parentRel.firstChild == ecs::NULL_ENTITY) {
                parentRel.firstChild = entity;
                parentRel.lastChild = entity;
            } else {
                registry.get<Relationship>(parentRel.lastChild).nextSibling = entity;
                parentRel.lastChild = entity;
            }
        } else {
            rel.level = 0;
            rel.nextSibling = ecs::NULL_ENTITY;
            rel.prevSibling = ecs::NULL_ENTITY;
            roots.push_back(entity);
        }
        hierarchyDirty = true;

        std::vector<ecs::Entity> stack;
        stack.push_back(entity);
        while (!stack.empty()) {
            ecs::Entity current = stack.back();
            stack.pop_back();
            if (!registry.has<TransformDirtyTag>(current)) {
                registry.emplace<TransformDirtyTag>(current);
            }

            if (!registry.has<Relationship>(current)) continue;
            ecs::Entity child = registry.get<Relationship>(current).firstChild;
            while (child != ecs::NULL_ENTITY) {
                stack.push_back(child);
                child = registry.get<Relationship>(child).nextSibling;
            }
        }
    }
}
