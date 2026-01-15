#pragma once

#include "Handle.h"
#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <array>
#include <thread>
#include <stdexcept>

namespace pnkr::core {

enum class SlotState : uint8_t {
    Free,
    Alive,
    Retired
};

template <typename T, typename Tag, uint32_t ChunkSize = 1024>
class StablePool {
public:
    using HandleType = Handle<Tag>;
    
    struct Slot {
        std::atomic<uint32_t> generation{0};
        std::atomic<SlotState> state{SlotState::Free};
        alignas(T) std::byte storage[sizeof(T)];
        std::atomic<uint32_t> refCount{0};

        T* get() { return reinterpret_cast<T*>(storage); }
        const T* get() const { return reinterpret_cast<const T*>(storage); }
    };

    StablePool() {
        // Pre-allocate first chunk
        allocateChunk();
    }

    ~StablePool() {
        for (auto& chunk : chunks_) {
            if (!chunk) continue;
            for (uint32_t i = 0; i < ChunkSize; ++i) {
                Slot& slot = chunk[i];
                if (slot.state.load(std::memory_order_relaxed) == SlotState::Alive) {
                    slot.get()->~T();
                }
            }
        }
    }

    void setRenderThreadId(std::thread::id id) {
        renderThreadId_ = id;
    }

    // Only Render Thread
    template <typename... Args>
    HandleType emplace(Args&&... args) {
        checkRenderThread();
        uint32_t index;
        if (!free_list_.empty()) {
            index = free_list_.back();
            free_list_.pop_back();
        } else {
            index = next_index_++;
            if (index >= chunks_.size() * ChunkSize) {
                allocateChunk();
            }
        }

        Slot& slot = getSlot(index);
        new (slot.storage) T(std::forward<Args>(args)...);
        
        // Start with refCount 0 (SmartHandle will increment it)
        slot.refCount.store(0, std::memory_order_relaxed);
        
        // Generation is already set from freeSlot (or 0 initially)
        // Transition Free -> Alive
        active_count_.fetch_add(1, std::memory_order_relaxed);
        slot.state.store(SlotState::Alive, std::memory_order_release);

        return HandleType(index, slot.generation.load(std::memory_order_relaxed));
    }

    // Only Render Thread
    // Moves state Alive -> Retired
    // MUST be called only after verifying refCount reached 0 via event
    void retire(HandleType handle) {
        checkRenderThread();
        if (!handle.isValid()) return;
        
        Slot& slot = getSlot(handle.index);
        
        // Validation: verify generation matches and state is Alive
        uint32_t currentGen = slot.generation.load(std::memory_order_relaxed);
        SlotState currentState = slot.state.load(std::memory_order_relaxed);

        if (currentGen == handle.generation && currentState == SlotState::Alive) {
            slot.state.store(SlotState::Retired, std::memory_order_release);
        } else {
            // Stale event or double-free attempt - ignore or log
        }
    }

    // Only Render Thread
    // Destroys T, bumps generation, moves state Retired -> Free
    void freeSlot(uint32_t index) {
        checkRenderThread();
        Slot& slot = getSlot(index);
        
        // Ensure we are retiring a valid slot
        if (slot.state.load(std::memory_order_relaxed) != SlotState::Retired) {
             return;
        }

        slot.get()->~T();
        active_count_.fetch_sub(1, std::memory_order_relaxed);

        uint32_t currentGen = slot.generation.load(std::memory_order_relaxed);
        uint32_t nextGen = (currentGen + 1) & 0xFFF; // 12-bit wrapping
        slot.generation.store(nextGen, std::memory_order_release);
        
        slot.state.store(SlotState::Free, std::memory_order_release);
        free_list_.push_back(index);
    }
    
    // Lock-free-ish (next_index_ is atomic-ish or just stable for Render thread)
    uint32_t size() const {
        return active_count_.load(std::memory_order_relaxed);
    }

    uint32_t capacity() const {
        return next_index_;
    }

    // Only Render Thread or during shutdown
    template <typename Func>
    void for_each(Func&& func) const {
        for (uint32_t i = 0; i < next_index_; ++i) {
            uint32_t chunkIdx = i / ChunkSize;
            uint32_t offset = i % ChunkSize;
            Slot* chunk = chunks_ptrs_[chunkIdx].load(std::memory_order_acquire);
            if (!chunk) continue;
            
            const Slot& slot = chunk[offset];
            if (slot.state.load(std::memory_order_acquire) == SlotState::Alive) {
                func(*slot.get(), HandleType(i, slot.generation.load(std::memory_order_relaxed)));
            }
        }
    }

    // Only Render Thread
    void clear() {
        checkRenderThread();
        // Since we are clearing everything, we don't strictly need to follow the 
        // Alive->Retired->Free dance for each, but we MUST destroy Alive objects.
        // And strictly speaking, other threads might hold refs. 
        // clear() implies forceful shutdown or reset.
        
        for (uint32_t i = 0; i < next_index_; ++i) {
             Slot& slot = getSlot(i);
             if (slot.state.load(std::memory_order_relaxed) == SlotState::Alive) {
                 slot.get()->~T();
                 slot.state.store(SlotState::Free, std::memory_order_relaxed);
             }
        }
        free_list_.clear();
        next_index_ = 0;
        // In a "stable" pool, we might choose NOT to release chunks to avoid pointer invalidation 
        // if this pool is reused. For now, we keep chunks.
        if (!chunks_.empty()) {
            // Reset logic if we want to reuse chunks from index 0
            // Simplified: we just reset tracking.
        }
    }

    // Lock-free validation and access
    T* get(HandleType handle) {
        if (!handle.isValid()) return nullptr;
        Slot* slot = getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get();
        }
        return nullptr;
    }

    const T* get(HandleType handle) const {
        if (!handle.isValid()) return nullptr;
        Slot* slot = const_cast<StablePool*>(this)->getSlotPtr(handle.index);
        if (slot && slot->state.load(std::memory_order_acquire) == SlotState::Alive && 
            slot->generation.load(std::memory_order_relaxed) == handle.generation) {
            return slot->get();
        }
        return nullptr;
    }

    // Lock-free
    const Slot* getSlotPtr(uint32_t index) const {
        uint32_t chunkIdx = index / ChunkSize;
        uint32_t offset = index % ChunkSize;
        if (chunkIdx >= kMaxChunks) return nullptr;
        
        Slot* chunk = chunks_ptrs_[chunkIdx].load(std::memory_order_acquire);
        if (!chunk) return nullptr;
        return &chunk[offset];
    }

    Slot* getSlotPtr(uint32_t index) {
        uint32_t chunkIdx = index / ChunkSize;
        uint32_t offset = index % ChunkSize;
        if (chunkIdx >= kMaxChunks) return nullptr;
        
        Slot* chunk = chunks_ptrs_[chunkIdx].load(std::memory_order_acquire);
        if (!chunk) return nullptr;
        return &chunk[offset];
    }

    // Lock-free validation
    bool validate(HandleType handle) const {
        if (!handle.isValid()) return false;
        uint32_t chunkIdx = handle.index / ChunkSize;
        uint32_t offset = handle.index % ChunkSize;
        if (chunkIdx >= kMaxChunks) return false;

        Slot* chunk = chunks_ptrs_[chunkIdx].load(std::memory_order_acquire);
        if (!chunk) return false;

        const Slot& slot = chunk[offset];
        // Order: load state (acquire), then generation (acquire)
        // If Alive, then generation must match.
        SlotState s = slot.state.load(std::memory_order_acquire);
        if (s != SlotState::Alive) return false;
        
        return slot.generation.load(std::memory_order_acquire) == handle.generation;
    }

private:
    static constexpr uint32_t kMaxChunks = 4096; // ~4M items max capacity
    mutable std::array<std::atomic<Slot*>, kMaxChunks> chunks_ptrs_{};
    std::vector<std::unique_ptr<Slot[]>> chunks_;
    std::vector<uint32_t> free_list_;
    uint32_t next_index_ = 0;
    std::atomic<uint32_t> active_count_{0};
    std::thread::id renderThreadId_;

    void allocateChunk() {
        auto newChunk = std::make_unique<Slot[]>(ChunkSize);
        // Ensure memory is committed before publishing pointer
        Slot* ptr = newChunk.get();
        uint32_t idx = static_cast<uint32_t>(chunks_.size());
        if (idx >= kMaxChunks) throw std::runtime_error("StablePool capacity exceeded");
        
        chunks_ptrs_[idx].store(ptr, std::memory_order_release);
        chunks_.push_back(std::move(newChunk));
    }

    Slot& getSlot(uint32_t index) {
        uint32_t chunkIdx = index / ChunkSize;
        uint32_t offset = index % ChunkSize;
        return chunks_[chunkIdx][offset];
    }
    
    void checkRenderThread() const {
        // If ID isn't set, we assume we are in early init or single-threaded context 
        // where strict checking might be skipped, but ideally it should be set.
        if (renderThreadId_ != std::thread::id() && std::this_thread::get_id() != renderThreadId_) {
            assert(false && "StablePool mutation must occur on Render Thread");
        }
    }
};

}
