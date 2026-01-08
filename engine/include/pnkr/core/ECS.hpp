#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <cassert>
#include <limits>
#include <tuple>
#include <atomic>
#include <span>
#include <concepts>

#include "pnkr/core/common.hpp"

namespace pnkr::ecs {

    template<typename T>
    concept Component = std::is_move_constructible_v<T> && std::is_destructible_v<T> && std::is_object_v<T>;

    using Entity = uint32_t;
    constexpr Entity NULL_ENTITY = std::numeric_limits<Entity>::max();

    uint32_t getUniqueComponentID();

    template <typename T>
    inline uint32_t getComponentTypeID() {
        static uint32_t typeID = getUniqueComponentID();
        return typeID;
    }

    class ISparseSet {
    public:
        virtual ~ISparseSet() = default;
        virtual void remove(Entity e) = 0;
        virtual bool has(Entity e) const = 0;
        virtual void clear() = 0;
        virtual size_t size() const = 0;
        virtual std::span<const Entity> entities() const = 0;
    };

    template <Component T>
    class SparseSet : public ISparseSet {
    private:
        static constexpr size_t PAGE_SIZE = 4096;
        static constexpr size_t NULL_INDEX = std::numeric_limits<size_t>::max();

        std::vector<T> dense;
        std::vector<Entity> packed;
        std::vector<std::unique_ptr<size_t[]>> sparsePages;

        size_t* getSparseIndex(Entity e) const {
            size_t page = e / PAGE_SIZE;
            size_t offset = e % PAGE_SIZE;
            if (page >= sparsePages.size() || !sparsePages[page]) {
                return nullptr;
            }
            return &sparsePages[page][offset];
        }

        size_t* ensureSparseIndex(Entity e) {
            size_t page = e / PAGE_SIZE;
            size_t offset = e % PAGE_SIZE;

            if (page >= sparsePages.size()) {
                sparsePages.resize(page + 1);
            }

            if (!sparsePages[page]) {
                sparsePages[page] = std::make_unique<size_t[]>(PAGE_SIZE);
                std::fill_n(sparsePages[page].get(), PAGE_SIZE, NULL_INDEX);
            }

            return &sparsePages[page][offset];
        }

    public:
        void reserve(size_t capacity) {
            dense.reserve(capacity);
            packed.reserve(capacity);
        }

        template<typename... Args>
        T& emplace(Entity e, Args&&... args) {
            size_t* idx = ensureSparseIndex(e);

            if (*idx != NULL_INDEX) {
                PNKR_ASSERT(*idx < dense.size(), "Sparse set corruption");
                dense[*idx] = T(std::forward<Args>(args)...);
                return dense[*idx];
            }

            *idx = dense.size();
            packed.push_back(e);
            dense.emplace_back(std::forward<Args>(args)...);
            return dense.back();
        }

        void remove(Entity e) override {
            size_t* idx = getSparseIndex(e);
            if (!idx || *idx == NULL_INDEX) return;

            size_t idxToRemove = *idx;
            size_t idxLast = dense.size() - 1;
            Entity entityLast = packed[idxLast];

            if (idxToRemove != idxLast) {
                dense[idxToRemove] = std::move(dense[idxLast]);
                packed[idxToRemove] = entityLast;

                size_t* idxSwap = getSparseIndex(entityLast);
                PNKR_ASSERT(idxSwap, "Sparse set corruption on swap");
                *idxSwap = idxToRemove;
            }

            *idx = NULL_INDEX;

            dense.pop_back();
            packed.pop_back();
        }

        bool has(Entity e) const override {
            size_t* idx = getSparseIndex(e);
            return idx && *idx != NULL_INDEX;
        }

        T& get(Entity e) {
            size_t* idx = getSparseIndex(e);
            PNKR_ASSERT(idx && *idx != NULL_INDEX, "Entity does not have component");
            return dense[*idx];
        }

        const T& get(Entity e) const {
            size_t* idx = getSparseIndex(e);
            PNKR_ASSERT(idx && *idx != NULL_INDEX, "Entity does not have component");
            return dense[*idx];
        }

        void clear() override {
            dense.clear();
            packed.clear();
            sparsePages.clear();
        }

        auto begin() { return dense.begin(); }
        auto end() { return dense.end(); }
        auto begin() const { return dense.begin(); }
        auto end() const { return dense.end(); }
        size_t size() const override { return dense.size(); }

        T* data() { return dense.data(); }
        const T* data() const { return dense.data(); }
        std::span<const Entity> entities() const override { return packed; }
        std::span<const T> getDense() const { return dense; }
        std::span<T> getDense() { return dense; }
    };

    class Registry;

    template <Component... Components>
    class View {
    public:
        Registry& reg;

        View(Registry& r) : reg(r) {}

        template <typename Func>
        void each(Func func) const;

        struct Iterator {
            Registry& reg;
            std::span<const Entity> entities;
            size_t index;

            Iterator(Registry& r, std::span<const Entity> list, size_t i)
                : reg(r), entities(list), index(i) {
                validate();
            }

            void validate();
            Entity operator*() const {
                PNKR_ASSERT(index < entities.size(), "ECS Iterator out of bounds");
                return entities[index];
            }
            Iterator& operator++() { index++; validate(); return *this; }
            bool operator!=(const Iterator& other) const { return index != other.index; }
        };

        Iterator begin();
        Iterator end();

    private:
        std::span<const Entity> smallestEntities() const;
    };

    class Registry {
    private:
        mutable std::vector<std::unique_ptr<ISparseSet>> componentPools;
        std::vector<Entity> freeEntities;
        Entity entityCounter = 0;

    public:
        Entity create();
        void destroy(Entity e);

        template <Component T>
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

        template <Component T>
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

        template <Component T, typename... Args>
        T& emplace(Entity e, Args&&... args) {
            return getPool<T>().emplace(e, std::forward<Args>(args)...);
        }

        template <Component T>
        void remove(Entity e) {
            getPool<T>().remove(e);
        }

        template <Component T>
        bool has(Entity e) const {
            uint32_t typeID = getComponentTypeID<T>();
            if (typeID >= componentPools.size() || !componentPools[typeID]) return false;
            return componentPools[typeID]->has(e);
        }

        template <Component T>
        T& get(Entity e) {
            return getPool<T>().get(e);
        }

        template <Component T>
        const T& get(Entity e) const {
            return getPool<T>().get(e);
        }

        template <Component... Args>
        View<Args...> view() {
            return View<Args...>(*this);
        }

        template <Component... Args>
        View<Args...> view() const {
            return View<Args...>(const_cast<Registry&>(*this));
        }

        void clear();
    };

    template <Component... Components>
    template <typename Func>
    void View<Components...>::each(Func func) const {
        if constexpr (sizeof...(Components) == 0) return;
        const auto entities = smallestEntities();
        for (Entity entity : entities) {
            if ((reg.has<Components>(entity) && ...)) {
                func(entity, reg.get<Components>(entity)...);
            }
        }
    }

    template <Component... Components>
    std::span<const Entity> View<Components...>::smallestEntities() const {
        using First = std::tuple_element_t<0, std::tuple<Components...>>;
        auto smallest = reg.getPool<First>().entities();
        size_t smallestSize = smallest.size();

        (void)std::initializer_list<int>{
            ([&] {
                using Comp = Components;
                auto entities = reg.getPool<Comp>().entities();
                if (entities.size() < smallestSize) {
                    smallest = entities;
                    smallestSize = entities.size();
                }
            }(), 0)...
        };

        return smallest;
    }

    template <Component... Components>
    void View<Components...>::Iterator::validate() {
        while (index < entities.size() && !(reg.has<Components>(entities[index]) && ...)) {
            index++;
        }
    }

    template <Component... Components>
    typename View<Components...>::Iterator View<Components...>::begin() {
        return Iterator(reg, smallestEntities(), 0);
    }

    template <Component... Components>
    typename View<Components...>::Iterator View<Components...>::end() {
        auto entities = smallestEntities();
        return Iterator(reg, entities, entities.size());
    }

    class EntityCommandBuffer {
    private:
        Registry& m_registry;
        std::vector<Entity> m_toCreate;
        std::vector<Entity> m_toDestroy;

    public:
        EntityCommandBuffer(Registry& reg);
        Entity create();
        void destroy(Entity e);
        void execute();
    };
}