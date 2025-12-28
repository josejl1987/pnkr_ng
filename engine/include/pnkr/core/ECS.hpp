#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <cassert>
#include <limits>
#include <tuple>
#include <atomic>

namespace pnkr::ecs {

    // 1. Entity Definition
    using Entity = uint32_t;
    constexpr Entity NULL_ENTITY = std::numeric_limits<Entity>::max();

    // 2. Component Type ID Generator
    inline uint32_t getUniqueComponentID() {
        static std::atomic<uint32_t> lastID{0};
        return lastID.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename T>
    inline uint32_t getComponentTypeID() {
        static uint32_t typeID = getUniqueComponentID();
        return typeID;
    }

    // 3. Sparse Set Interface
    class ISparseSet {
    public:
        virtual ~ISparseSet() = default;
        virtual void remove(Entity e) = 0;
        virtual bool has(Entity e) const = 0;
        virtual void clear() = 0;
        virtual size_t size() const = 0;
        virtual const std::vector<Entity>& entities() const = 0;
    };

    // 4. Sparse Set Implementation
    template <typename T>
    class SparseSet : public ISparseSet {
    private:
        std::vector<T> dense;
        std::vector<Entity> packed;
        std::vector<size_t> sparse;

    public:
        void reserve(size_t capacity) {
            dense.reserve(capacity);
            packed.reserve(capacity);
        }

        template<typename... Args>
        T& emplace(Entity e, Args&&... args) {
            if (e >= sparse.size()) {
                sparse.resize(e + 1, std::numeric_limits<size_t>::max());
            }

            if (sparse[e] != std::numeric_limits<size_t>::max()) {
                dense[sparse[e]] = T(std::forward<Args>(args)...);
                return dense[sparse[e]];
            }

            sparse[e] = dense.size();
            packed.push_back(e);
            dense.emplace_back(std::forward<Args>(args)...);
            return dense.back();
        }

        void remove(Entity e) override {
            if (!has(e)) return;

            size_t idxToRemove = sparse[e];
            size_t idxLast = dense.size() - 1;
            Entity entityLast = packed[idxLast];

            dense[idxToRemove] = std::move(dense[idxLast]);
            packed[idxToRemove] = entityLast;

            sparse[entityLast] = idxToRemove;
            sparse[e] = std::numeric_limits<size_t>::max();

            dense.pop_back();
            packed.pop_back();
        }

        bool has(Entity e) const override {
            return e < sparse.size() && sparse[e] != std::numeric_limits<size_t>::max();
        }

        T& get(Entity e) {
            assert(has(e));
            return dense[sparse[e]];
        }

        const T& get(Entity e) const {
            assert(has(e));
            return dense[sparse[e]];
        }
        
        void clear() override {
            dense.clear();
            packed.clear();
            std::fill(sparse.begin(), sparse.end(), std::numeric_limits<size_t>::max());
        }

        auto begin() { return dense.begin(); }
        auto end() { return dense.end(); }
        auto begin() const { return dense.begin(); }
        auto end() const { return dense.end(); }
        size_t size() const override { return dense.size(); }
        
        T* data() { return dense.data(); }
        const T* data() const { return dense.data(); }
        const std::vector<Entity>& entities() const override { return packed; }
        const std::vector<T>& getDense() const { return dense; }
        std::vector<T>& getDense() { return dense; }
    };

    class Registry;

    // 5. Multi-Component View
    template <typename... Components>
    class View {
    public:
        Registry& reg;

        View(Registry& r) : reg(r) {}

        template <typename Func>
        void each(Func func) const;

        struct Iterator {
            Registry& reg;
            const std::vector<Entity>& entities;
            size_t index;

            Iterator(Registry& r, const std::vector<Entity>& list, size_t i) 
                : reg(r), entities(list), index(i) {
                validate();
            }

            void validate();
            Entity operator*() const { return entities[index]; }
            Iterator& operator++() { index++; validate(); return *this; }
            bool operator!=(const Iterator& other) const { return index != other.index; }
        };

        Iterator begin();
        Iterator end();

    private:
        const std::vector<Entity>& smallestEntities() const;
    };

    // 6. Registry (The World)
    class Registry {
    private:
        mutable std::vector<std::unique_ptr<ISparseSet>> componentPools;
        std::vector<Entity> freeEntities;
        Entity entityCounter = 0;

    public:
        Entity create() {
            if (!freeEntities.empty()) {
                Entity e = freeEntities.back();
                freeEntities.pop_back();
                return e;
            }
            return entityCounter++;
        }

        void destroy(Entity e) {
            for (auto& pool : componentPools) {
                if (pool) pool->remove(e);
            }
            freeEntities.push_back(e);
        }

        template <typename T>
        SparseSet<T>& getPool() {
            uint32_t typeID = getComponentTypeID<T>();
            if (typeID >= componentPools.size()) {
                componentPools.resize(typeID + 1);
            }
            if (!componentPools[typeID]) {
                componentPools[typeID] = std::make_unique<SparseSet<T>>();
            }
            return *static_cast<SparseSet<T>*>(componentPools[typeID].get());
        }

        template <typename T>
        const SparseSet<T>& getPool() const {
            uint32_t typeID = getComponentTypeID<T>();
            if (typeID >= componentPools.size() || !componentPools[typeID]) {
                componentPools.resize(std::max<size_t>(componentPools.size(), typeID + 1));
                if (!componentPools[typeID]) {
                    componentPools[typeID] = std::make_unique<SparseSet<T>>();
                }
            }
            return *static_cast<const SparseSet<T>*>(componentPools[typeID].get());
        }

        template <typename T, typename... Args>
        T& emplace(Entity e, Args&&... args) {
            return getPool<T>().emplace(e, std::forward<Args>(args)...);
        }

        template <typename T>
        void remove(Entity e) {
            getPool<T>().remove(e);
        }

        template <typename T>
        bool has(Entity e) const {
            uint32_t typeID = getComponentTypeID<T>();
            if (typeID >= componentPools.size() || !componentPools[typeID]) return false;
            return componentPools[typeID]->has(e);
        }

        template <typename T>
        T& get(Entity e) {
            return getPool<T>().get(e);
        }

        template <typename T>
        const T& get(Entity e) const {
            return getPool<T>().get(e);
        }

        template <typename... Args>
        View<Args...> view() {
            return View<Args...>(*this);
        }

        template <typename... Args>
        View<Args...> view() const {
            return View<Args...>(const_cast<Registry&>(*this));
        }
        
        void clear() {
            for(auto& pool : componentPools) {
                if(pool) pool->clear();
            }
            entityCounter = 0;
            freeEntities.clear();
        }
    };

    // View implementation
    template <typename... Components>
    template <typename Func>
    void View<Components...>::each(Func func) const {
        if constexpr (sizeof...(Components) == 0) return;
        const auto& entities = smallestEntities();
        for (Entity entity : entities) {
            if ((reg.has<Components>(entity) && ...)) {
                func(entity, reg.get<Components>(entity)...);
            }
        }
    }

    template <typename... Components>
    const std::vector<Entity>& View<Components...>::smallestEntities() const {
        using First = std::tuple_element_t<0, std::tuple<Components...>>;
        const std::vector<Entity>* smallest = &reg.getPool<First>().entities();
        size_t smallestSize = smallest->size();

        (void)std::initializer_list<int>{
            ([&] {
                using Comp = Components;
                const auto& entities = reg.getPool<Comp>().entities();
                if (entities.size() < smallestSize) {
                    smallest = &entities;
                    smallestSize = entities.size();
                }
            }(), 0)...
        };

        return *smallest;
    }

    template <typename... Components>
    void View<Components...>::Iterator::validate() {
        while (index < entities.size() && !(reg.has<Components>(entities[index]) && ...)) {
            index++;
        }
    }

    template <typename... Components>
    typename View<Components...>::Iterator View<Components...>::begin() {
        return Iterator(reg, smallestEntities(), 0);
    }

    template <typename... Components>
    typename View<Components...>::Iterator View<Components...>::end() {
        const auto& entities = smallestEntities();
        return Iterator(reg, entities, entities.size());
    }

    // 7. Command Buffer
    class EntityCommandBuffer {
    private:
        Registry& m_registry;
        std::vector<Entity> m_toCreate;
        std::vector<Entity> m_toDestroy;

    public:
        EntityCommandBuffer(Registry& reg) : m_registry(reg) {}

        Entity create() {
            Entity e = m_registry.create();
            m_toCreate.push_back(e);
            return e;
        }

        void destroy(Entity e) {
            m_toDestroy.push_back(e);
        }

        void execute() {
            for (Entity e : m_toDestroy) {
                m_registry.destroy(e);
            }
            m_toCreate.clear();
            m_toDestroy.clear();
        }
    };
}
