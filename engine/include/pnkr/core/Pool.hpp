#pragma once

#include "Handle.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <cassert>
#include <memory>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <functional>

#include "pnkr/core/common.hpp"

namespace pnkr::core {

template <typename T, typename Tag>
class Pool {
public:
    using HandleType = Handle<Tag>;
    static constexpr uint32_t kMaxCapacity = HandleType::kInvalidIndex;

    struct Slot {
        std::optional<T> storage;
        uint32_t generation = 0;

        Slot() = default;
        ~Slot() = default;

        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;

        Slot(Slot&& other) noexcept : storage(std::move(other.storage)), generation(other.generation) {
        }

        Slot& operator=(Slot&& other) noexcept {
            if (this != &other) {
                storage = std::move(other.storage);
                generation = other.generation;
            }
            return *this;
        }

        bool occupied() const { return storage.has_value(); }
    };

    Pool() = default;
    ~Pool() = default;

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) noexcept = default;
    Pool& operator=(Pool&&) noexcept = default;

    template <typename... Args>
    HandleType emplace(Args&&... args) {
        uint32_t index = 0;

        if (!free_list_.empty()) {
            index = free_list_.back();
            free_list_.pop_back();
        } else {
            index = static_cast<uint32_t>(slots_.size());
            if (index >= kMaxCapacity) {
                throw std::runtime_error("Pool capacity exhausted");
            }
            slots_.emplace_back();
        }

        PNKR_ASSERT(index < slots_.size(), "Pool index out of range");
        Slot& slot = slots_[index];
        slot.storage.emplace(std::forward<Args>(args)...);

        return HandleType(index, slot.generation);
    }

    bool erase(HandleType handle) {
        if (!validate(handle)) {
            return false;
        }

        PNKR_ASSERT(handle.index < slots_.size(), "Pool index out of range");
        Slot& slot = slots_[handle.index];
        slot.storage.reset();

        slot.generation = (slot.generation + 1) & ((1 << 12) - 1);
        free_list_.push_back(handle.index);

        return true;
    }

    [[nodiscard]] T* get(HandleType handle) {
        if (!validate(handle)) {
            return nullptr;
        }
        PNKR_ASSERT(handle.index < slots_.size(), "Pool index out of range");
        return &(*slots_[handle.index].storage);
    }

    [[nodiscard]] const T* get(HandleType handle) const {
        if (!validate(handle)) {
            return nullptr;
        }
        PNKR_ASSERT(handle.index < slots_.size(), "Pool index out of range");
        return &(*slots_[handle.index].storage);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<T>> getRef(HandleType handle) {
        if (!validate(handle)) {
            return std::nullopt;
        }
        return std::ref(*slots_[handle.index].storage);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const T>> getRef(HandleType handle) const {
        if (!validate(handle)) {
            return std::nullopt;
        }
        return std::cref(*slots_[handle.index].storage);
    }

    bool validate(HandleType handle) const noexcept {
        if (!handle.isValid() || handle.index >= slots_.size()) {
            return false;
        }
        const Slot& slot = slots_[handle.index];
        return slot.occupied() && slot.generation == handle.generation;
    }

    size_t size() const noexcept {
        return slots_.size() - free_list_.size();
    }

    size_t capacity() const noexcept {
        return slots_.size();
    }

    bool empty() const noexcept {
        return size() == 0;
    }

    void clear() {
        for (auto& slot : slots_) {
            if (slot.occupied()) {
                slot.storage.reset();
                slot.generation = (slot.generation + 1) & ((1 << 12) - 1);
            }
        }
        free_list_.clear();
    }

    void reserve(size_t capacity) {
        slots_.reserve(std::min(capacity, static_cast<size_t>(kMaxCapacity)));
    }

    template <typename Func>
    void for_each(Func&& func) {
        for (size_t i = 0; i < slots_.size(); ++i) {
            Slot& slot = slots_[i];
            if (slot.occupied()) {
                HandleType handle(static_cast<uint32_t>(i), slot.generation);
                func(*slot.storage, handle);
            }
        }
    }

    template <typename Func>
    void for_each(Func&& func) const {
        for (size_t i = 0; i < slots_.size(); ++i) {
            const Slot& slot = slots_[i];
            if (slot.occupied()) {
                HandleType handle(static_cast<uint32_t>(i), slot.generation);
                func(*slot.storage, handle);
            }
        }
    }

    std::span<Slot> slots() { return slots_; }
    std::span<const Slot> slots() const { return slots_; }

private:
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_list_;
};

}
