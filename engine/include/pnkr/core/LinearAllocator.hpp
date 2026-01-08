#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "pnkr/core/common.hpp"

namespace pnkr::core {

class LinearAllocator {
public:
    explicit LinearAllocator(size_t sizeBytes = 64 * 1024 * 1024)
        : m_totalSize(sizeBytes),
          m_start(std::make_unique<uint8_t[]>(sizeBytes)) {
        m_current = m_start.get();
    }

    ~LinearAllocator() = default;

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    LinearAllocator(LinearAllocator&& other) noexcept
        : m_totalSize(other.m_totalSize),
          m_start(std::move(other.m_start)),
          m_current(other.m_current) {
        other.m_current = nullptr;
        other.m_totalSize = 0;
    }

    LinearAllocator& operator=(LinearAllocator&& other) noexcept {
        if (this != &other) {
            m_start = std::move(other.m_start);
            m_current = other.m_current;
            m_totalSize = other.m_totalSize;
            other.m_current = nullptr;
            other.m_totalSize = 0;
        }
        return *this;
    }

    void reset() {
        m_current = m_start.get();
    }

    template <typename T>
    T* alloc(size_t count = 1, size_t alignment = alignof(T)) {
        void* ptr = m_current;
        size_t space = m_totalSize - usedBytes();
        size_t sizeBytes = count * sizeof(T);

        if (std::align(alignment, sizeBytes, ptr, space)) {
            m_current = static_cast<uint8_t*>(ptr) + sizeBytes;
            return static_cast<T*>(ptr);
        }

        PNKR_ASSERT(false, "LinearAllocator overflow! Increase buffer size.");
        return nullptr;
    }

    struct Marker {
        size_t offset;
    };

    Marker mark() const {
        return Marker{ usedBytes() };
    }

    void rewind(Marker m) {
        m_current = m_start.get() + m.offset;
    }

    size_t usedBytes() const {
        return static_cast<size_t>(m_current - m_start.get());
    }

    const uint8_t* data() const { return m_start.get(); }
    uint8_t* data() { return m_start.get(); }

private:
    size_t m_totalSize = 0;
    std::unique_ptr<uint8_t[]> m_start;
    uint8_t* m_current = nullptr;
};

}