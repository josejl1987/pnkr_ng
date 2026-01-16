#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include <algorithm>
#include <stack>

namespace pnkr::renderer::scene {

    ecs::Entity SceneGraphDOD::createNode(ecs::Entity parent) {
        ecs::Entity entity = m_registry.create();
        if (m_root == ecs::kNullEntity) {
          m_root = entity;
        }

        m_registry.emplace<LocalTransform>(entity);
        m_registry.emplace<WorldTransform>(entity);
        auto &rel = m_registry.emplace<Relationship>(entity);

        if (parent != ecs::kNullEntity) {
            rel.setParent(parent);
            auto &parentRel = m_registry.get<Relationship>(parent);
            rel.setLevel(static_cast<uint16_t>(parentRel.level() + 1));

            if (parentRel.firstChild() == ecs::kNullEntity) {
                parentRel.setFirstChild(entity);
                parentRel.setLastChild(entity);
            } else {
                ecs::Entity lastChild = parentRel.lastChild();
                m_registry.get<Relationship>(lastChild).setNextSibling(entity);
                rel.setPrevSibling(lastChild);
                parentRel.setLastChild(entity);
            }
        } else {
            m_roots.push_back(entity);
            rel.setLevel(0);
        }

        m_hierarchyDirty = true;
        return entity;
    }

    void SceneGraphDOD::destroyNode(ecs::Entity entity) {
      if (!m_registry.has<Relationship>(entity)) {
        return;
      }

      auto &rel = m_registry.get<Relationship>(entity);

      ecs::Entity child = rel.firstChild();
      while (child != ecs::kNullEntity) {
        ecs::Entity next = m_registry.get<Relationship>(child).nextSibling();
        destroyNode(child);
        child = next;
      }

        if (rel.parent() != ecs::kNullEntity) {
          auto &parentRel = m_registry.get<Relationship>(rel.parent());
          if (parentRel.firstChild() == entity) {
            parentRel.setFirstChild(rel.nextSibling());
          }
            if (parentRel.lastChild() == entity) {
                parentRel.setLastChild(rel.prevSibling());
            }
        } else {

          auto it = std::ranges::find(m_roots, entity);
          if (it != m_roots.end()) {
            m_roots.erase(it);
          }
        }

        if (rel.prevSibling() != ecs::kNullEntity) {
            m_registry.get<Relationship>(rel.prevSibling()).setNextSibling(rel.nextSibling());
        }
        if (rel.nextSibling() != ecs::kNullEntity) {
            m_registry.get<Relationship>(rel.nextSibling()).setPrevSibling(rel.prevSibling());
        }

        if (m_root == entity) {
          m_root = ecs::kNullEntity;
        }

        m_registry.destroy(entity);
        m_hierarchyDirty = true;
    }

    void SceneGraphDOD::updateTopoOrder() {
        m_topoOrder.clear();
        std::stack<ecs::Entity> stack;
        for (auto it = m_roots.rbegin(); it != m_roots.rend(); ++it) {
            stack.push(*it);
        }

        while (!stack.empty()) {
            ecs::Entity e = stack.top();
            stack.pop();
            m_topoOrder.push_back(e);

            auto &rel = m_registry.get<Relationship>(e);

            ecs::Entity child = rel.firstChild();
            std::vector<ecs::Entity> children;
            while (child != ecs::kNullEntity) {
                children.push_back(child);
                child = m_registry.get<Relationship>(child).nextSibling();
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push(*it);
            }
        }
        m_hierarchyDirty = false;
    }

    void SceneGraphDOD::recalculateGlobalTransformsFull() {
      if (m_hierarchyDirty) {
        updateTopoOrder();
      }

        for (ecs::Entity e : m_topoOrder) {
            const Relationship& rel = m_registry.get<Relationship>(e);
            const glm::mat4& local = m_registry.get<LocalTransform>(e).matrix;

            if (rel.parent() != ecs::kNullEntity) {
                m_registry.get<WorldTransform>(e).matrix = m_registry.get<WorldTransform>(rel.parent()).matrix * local;
            } else {
                m_registry.get<WorldTransform>(e).matrix = local;
            }
        }
        m_registry.getPool<TransformDirtyTag>().clear();
    }

    void SceneGraphDOD::updateTransforms() {
        if (m_hierarchyDirty) {
            recalculateGlobalTransformsFull();
            return;
        }

        if (m_registry.getPool<TransformDirtyTag>().size() == 0) {
          return;
        }

        for (ecs::Entity e : m_topoOrder) {
          auto &rel = m_registry.get<Relationship>(e);
          bool isDirty = m_registry.has<TransformDirtyTag>(e);

          if (!isDirty && rel.parent() != ecs::kNullEntity) {
            if (m_registry.has<TransformDirtyTag>(rel.parent())) {
              m_registry.emplace<TransformDirtyTag>(e);
              if (!m_registry.has<BoundsDirtyTag>(e)) {
                m_registry.emplace<BoundsDirtyTag>(e);
              }
              isDirty = true;
            }
            }

            if (isDirty) {
                if (!m_registry.has<BoundsDirtyTag>(e)) {
                    m_registry.emplace<BoundsDirtyTag>(e);
                }
                const glm::mat4& local = m_registry.get<LocalTransform>(e).matrix;
                if (rel.parent() != ecs::kNullEntity) {
                    m_registry.get<WorldTransform>(e).matrix = m_registry.get<WorldTransform>(rel.parent()).matrix * local;
                } else {
                    m_registry.get<WorldTransform>(e).matrix = local;
                }
            }
        }

        m_registry.getPool<TransformDirtyTag>().clear();
    }

    void SceneGraphDOD::markAsChanged(ecs::Entity entity) {
        if (!m_registry.has<TransformDirtyTag>(entity)) {
            m_registry.emplace<TransformDirtyTag>(entity);
        }
        if (!m_registry.has<BoundsDirtyTag>(entity)) {
            m_registry.emplace<BoundsDirtyTag>(entity);
        }
    }

    void SceneGraphDOD::onHierarchyChanged() {
        m_hierarchyDirty = true;
        updateTopoOrder();
    }

    void SceneGraphDOD::setParent(ecs::Entity entity, ecs::Entity parent) {
      if (entity == parent) {
        return;
      }
        if (parent != ecs::kNullEntity && m_registry.getPool<Relationship>().has(parent)) {
            ecs::Entity current = parent;
            while (current != ecs::kNullEntity) {
              if (current == entity) {
                return;
              }
              auto &rel = m_registry.get<Relationship>(current);
              current = rel.parent();
            }
        }
        Relationship& rel = m_registry.getPool<Relationship>().has(entity) ?
            m_registry.get<Relationship>(entity) :
            m_registry.emplace<Relationship>(entity);

        if (rel.parent() != ecs::kNullEntity) {
          auto &oldParentRel = m_registry.get<Relationship>(rel.parent());
          if (oldParentRel.firstChild() == entity) {
            oldParentRel.setFirstChild(rel.nextSibling());
          }
          if (oldParentRel.lastChild() == entity) {
            oldParentRel.setLastChild(rel.prevSibling());
          }

          if (rel.prevSibling() != ecs::kNullEntity) {
            m_registry.get<Relationship>(rel.prevSibling()).setNextSibling(
                rel.nextSibling());
          }
          if (rel.nextSibling() != ecs::kNullEntity) {
            m_registry.get<Relationship>(rel.nextSibling()).setPrevSibling(
                rel.prevSibling());
          }
        } else {
          auto it = std::ranges::find(m_roots, entity);
          if (it != m_roots.end()) {
            m_roots.erase(it);
          }
        }

        rel.setParent(parent);
        if (parent != ecs::kNullEntity) {
            Relationship& parentRel = m_registry.getPool<Relationship>().has(parent) ?
                m_registry.get<Relationship>(parent) :
                m_registry.emplace<Relationship>(parent);

            rel.setLevel(static_cast<uint16_t>(parentRel.level() + 1));
            rel.setNextSibling(ecs::kNullEntity);
            rel.setPrevSibling(parentRel.lastChild());

            if (parentRel.firstChild() == ecs::kNullEntity) {
                parentRel.setFirstChild(entity);
                parentRel.setLastChild(entity);
            } else {
                m_registry.get<Relationship>(parentRel.lastChild()).setNextSibling(entity);
                parentRel.setLastChild(entity);
            }
        } else {
            rel.setLevel(0);
            rel.setNextSibling(ecs::kNullEntity);
            rel.setPrevSibling(ecs::kNullEntity);
            m_roots.push_back(entity);
        }
        m_hierarchyDirty = true;

        std::vector<ecs::Entity> stack;
        stack.push_back(entity);
        while (!stack.empty()) {
            ecs::Entity current = stack.back();
            stack.pop_back();
            if (!m_registry.has<TransformDirtyTag>(current)) {
                m_registry.emplace<TransformDirtyTag>(current);
            }
            if (!m_registry.has<BoundsDirtyTag>(current)) {
                m_registry.emplace<BoundsDirtyTag>(current);
            }

            if (!m_registry.has<Relationship>(current)) {
              continue;
            }
            ecs::Entity child = m_registry.get<Relationship>(current).firstChild();
            while (child != ecs::kNullEntity) {
                stack.push_back(child);
                child = m_registry.get<Relationship>(child).nextSibling();
            }
        }
    }
}
