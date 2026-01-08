#include "pnkr/core/ECS.hpp"

namespace pnkr::ecs {

    uint32_t getUniqueComponentID() {
        static std::atomic<uint32_t> lastID{0};
        return lastID.fetch_add(1, std::memory_order_relaxed);
    }

    Entity Registry::create() {
        if (!freeEntities.empty()) {
            Entity entity = freeEntities.back();
            freeEntities.pop_back();
            return entity;
        }
        return entityCounter++;
    }

    void Registry::destroy(Entity entity) {
        for (auto& pool : componentPools) {
          if (pool) {
            pool->remove(entity);
          }
        }
        freeEntities.push_back(entity);
    }

    void Registry::clear() {
        for (auto& pool : componentPools) {
          if (pool) {
            pool->clear();
          }
        }
        entityCounter = 0;
        freeEntities.clear();
    }

    EntityCommandBuffer::EntityCommandBuffer(Registry& reg) : m_registry(reg) {}

    Entity EntityCommandBuffer::create() {
        Entity entity = m_registry.create();
        m_toCreate.push_back(entity);
        return entity;
    }

    void EntityCommandBuffer::destroy(Entity entity) {
        m_toDestroy.push_back(entity);
    }

    void EntityCommandBuffer::execute() {
        for (Entity entity : m_toDestroy) {
            m_registry.destroy(entity);
        }
        m_toCreate.clear();
        m_toDestroy.clear();
    }
}
