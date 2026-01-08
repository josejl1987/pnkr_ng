#include "pnkr/renderer/scene/SceneGraph.hpp"
#include "pnkr/renderer/scene/Bounds.hpp"
#include <algorithm>
#include <stack>

namespace pnkr::renderer::scene {

    ecs::Entity SceneGraphDOD::createNode(ecs::Entity parent) {
        ecs::Entity entity = registry_.create();
        if (root_ == ecs::NULL_ENTITY) {
          root_ = entity;
        }

        registry_.emplace<LocalTransform>(entity);
        registry_.emplace<WorldTransform>(entity);
        auto &rel = registry_.emplace<Relationship>(entity);

        if (parent != ecs::NULL_ENTITY) {
            rel.setParent(parent);
            auto &parentRel = registry_.get<Relationship>(parent);
            rel.setLevel(static_cast<uint16_t>(parentRel.level() + 1));

            if (parentRel.firstChild() == ecs::NULL_ENTITY) {
                parentRel.setFirstChild(entity);
                parentRel.setLastChild(entity);
            } else {
                ecs::Entity lastChild = parentRel.lastChild();
                registry_.get<Relationship>(lastChild).setNextSibling(entity);
                rel.setPrevSibling(lastChild);
                parentRel.setLastChild(entity);
            }
        } else {
            roots_.push_back(entity);
            rel.setLevel(0);
        }

        hierarchyDirty_ = true;
        return entity;
    }

    void SceneGraphDOD::destroyNode(ecs::Entity entity) {
      if (!registry_.has<Relationship>(entity)) {
        return;
      }

      auto &rel = registry_.get<Relationship>(entity);

      ecs::Entity child = rel.firstChild();
      while (child != ecs::NULL_ENTITY) {
        ecs::Entity next = registry_.get<Relationship>(child).nextSibling();
        destroyNode(child);
        child = next;
      }

        if (rel.parent() != ecs::NULL_ENTITY) {
          auto &parentRel = registry_.get<Relationship>(rel.parent());
          if (parentRel.firstChild() == entity) {
            parentRel.setFirstChild(rel.nextSibling());
          }
            if (parentRel.lastChild() == entity) {
                parentRel.setLastChild(rel.prevSibling());
            }
        } else {

          auto it = std::ranges::find(roots_, entity);
          if (it != roots_.end()) {
            roots_.erase(it);
          }
        }

        if (rel.prevSibling() != ecs::NULL_ENTITY) {
            registry_.get<Relationship>(rel.prevSibling()).setNextSibling(rel.nextSibling());
        }
        if (rel.nextSibling() != ecs::NULL_ENTITY) {
            registry_.get<Relationship>(rel.nextSibling()).setPrevSibling(rel.prevSibling());
        }

        if (root_ == entity) {
          root_ = ecs::NULL_ENTITY;
        }

        registry_.destroy(entity);
        hierarchyDirty_ = true;
    }

    void SceneGraphDOD::updateTopoOrder() {
        topoOrder_.clear();
        std::stack<ecs::Entity> stack;
        for (auto it = roots_.rbegin(); it != roots_.rend(); ++it) {
            stack.push(*it);
        }

        while (!stack.empty()) {
            ecs::Entity e = stack.top();
            stack.pop();
            topoOrder_.push_back(e);

            auto &rel = registry_.get<Relationship>(e);

            ecs::Entity child = rel.firstChild();
            std::vector<ecs::Entity> children;
            while (child != ecs::NULL_ENTITY) {
                children.push_back(child);
                child = registry_.get<Relationship>(child).nextSibling();
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push(*it);
            }
        }
        hierarchyDirty_ = false;
    }

    void SceneGraphDOD::recalculateGlobalTransformsFull() {
      if (hierarchyDirty_) {
        updateTopoOrder();
      }

        for (ecs::Entity e : topoOrder_) {
            const Relationship& rel = registry_.get<Relationship>(e);
            const glm::mat4& local = registry_.get<LocalTransform>(e).matrix;

            if (rel.parent() != ecs::NULL_ENTITY) {
                registry_.get<WorldTransform>(e).matrix = registry_.get<WorldTransform>(rel.parent()).matrix * local;
            } else {
                registry_.get<WorldTransform>(e).matrix = local;
            }
        }
        registry_.getPool<TransformDirtyTag>().clear();
    }

    void SceneGraphDOD::updateTransforms() {
        if (hierarchyDirty_) {
            recalculateGlobalTransformsFull();
            return;
        }

        if (registry_.getPool<TransformDirtyTag>().size() == 0) {
          return;
        }

        for (ecs::Entity e : topoOrder_) {
          auto &rel = registry_.get<Relationship>(e);
          bool isDirty = registry_.has<TransformDirtyTag>(e);

          if (!isDirty && rel.parent() != ecs::NULL_ENTITY) {
            if (registry_.has<TransformDirtyTag>(rel.parent())) {
              registry_.emplace<TransformDirtyTag>(e);
              if (!registry_.has<BoundsDirtyTag>(e)) {
                registry_.emplace<BoundsDirtyTag>(e);
              }
              isDirty = true;
            }
            }

            if (isDirty) {
                if (!registry_.has<BoundsDirtyTag>(e)) {
                    registry_.emplace<BoundsDirtyTag>(e);
                }
                const glm::mat4& local = registry_.get<LocalTransform>(e).matrix;
                if (rel.parent() != ecs::NULL_ENTITY) {
                    registry_.get<WorldTransform>(e).matrix = registry_.get<WorldTransform>(rel.parent()).matrix * local;
                } else {
                    registry_.get<WorldTransform>(e).matrix = local;
                }
            }
        }

        registry_.getPool<TransformDirtyTag>().clear();
    }

    void SceneGraphDOD::markAsChanged(ecs::Entity entity) {
        if (!registry_.has<TransformDirtyTag>(entity)) {
            registry_.emplace<TransformDirtyTag>(entity);
        }
        if (!registry_.has<BoundsDirtyTag>(entity)) {
            registry_.emplace<BoundsDirtyTag>(entity);
        }
    }

    void SceneGraphDOD::onHierarchyChanged() {
        hierarchyDirty_ = true;
        updateTopoOrder();
    }

    void SceneGraphDOD::setParent(ecs::Entity entity, ecs::Entity parent) {
      if (entity == parent) {
        return;
      }
        if (parent != ecs::NULL_ENTITY && registry_.getPool<Relationship>().has(parent)) {
            ecs::Entity current = parent;
            while (current != ecs::NULL_ENTITY) {
              if (current == entity) {
                return;
              }
              auto &rel = registry_.get<Relationship>(current);
              current = rel.parent();
            }
        }
        Relationship& rel = registry_.getPool<Relationship>().has(entity) ?
            registry_.get<Relationship>(entity) :
            registry_.emplace<Relationship>(entity);

        if (rel.parent() != ecs::NULL_ENTITY) {
          auto &oldParentRel = registry_.get<Relationship>(rel.parent());
          if (oldParentRel.firstChild() == entity) {
            oldParentRel.setFirstChild(rel.nextSibling());
          }
          if (oldParentRel.lastChild() == entity) {
            oldParentRel.setLastChild(rel.prevSibling());
          }

          if (rel.prevSibling() != ecs::NULL_ENTITY) {
            registry_.get<Relationship>(rel.prevSibling()).setNextSibling(
                rel.nextSibling());
          }
          if (rel.nextSibling() != ecs::NULL_ENTITY) {
            registry_.get<Relationship>(rel.nextSibling()).setPrevSibling(
                rel.prevSibling());
          }
        } else {
          auto it = std::ranges::find(roots_, entity);
          if (it != roots_.end()) {
            roots_.erase(it);
          }
        }

        rel.setParent(parent);
        if (parent != ecs::NULL_ENTITY) {
            Relationship& parentRel = registry_.getPool<Relationship>().has(parent) ?
                registry_.get<Relationship>(parent) :
                registry_.emplace<Relationship>(parent);

            rel.setLevel(static_cast<uint16_t>(parentRel.level() + 1));
            rel.setNextSibling(ecs::NULL_ENTITY);
            rel.setPrevSibling(parentRel.lastChild());

            if (parentRel.firstChild() == ecs::NULL_ENTITY) {
                parentRel.setFirstChild(entity);
                parentRel.setLastChild(entity);
            } else {
                registry_.get<Relationship>(parentRel.lastChild()).setNextSibling(entity);
                parentRel.setLastChild(entity);
            }
        } else {
            rel.setLevel(0);
            rel.setNextSibling(ecs::NULL_ENTITY);
            rel.setPrevSibling(ecs::NULL_ENTITY);
            roots_.push_back(entity);
        }
        hierarchyDirty_ = true;

        std::vector<ecs::Entity> stack;
        stack.push_back(entity);
        while (!stack.empty()) {
            ecs::Entity current = stack.back();
            stack.pop_back();
            if (!registry_.has<TransformDirtyTag>(current)) {
                registry_.emplace<TransformDirtyTag>(current);
            }
            if (!registry_.has<BoundsDirtyTag>(current)) {
                registry_.emplace<BoundsDirtyTag>(current);
            }

            if (!registry_.has<Relationship>(current)) {
              continue;
            }
            ecs::Entity child = registry_.get<Relationship>(current).firstChild();
            while (child != ecs::NULL_ENTITY) {
                stack.push_back(child);
                child = registry_.get<Relationship>(child).nextSibling();
            }
        }
    }
}
