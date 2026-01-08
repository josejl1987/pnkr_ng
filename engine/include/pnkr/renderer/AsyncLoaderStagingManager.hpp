#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <array>

namespace pnkr::renderer
{
    class RHIRenderer;

    struct StagingBuffer
    {
        BufferPtr handle;
        rhi::RHIBuffer* buffer = nullptr;
        uint8_t* mapped = nullptr;
        uint64_t size = 0;
        std::atomic<bool> inUse{false};
    };

    class AsyncLoaderStagingManager
    {
    public:
        explicit AsyncLoaderStagingManager(RHIRenderer* renderer);
        ~AsyncLoaderStagingManager();

        AsyncLoaderStagingManager(const AsyncLoaderStagingManager&) = delete;
        AsyncLoaderStagingManager& operator=(const AsyncLoaderStagingManager&) = delete;

        uint8_t* ringBufferMapped() const { return m_ringBufferMapped; }
        rhi::RHIBuffer* ringBuffer() const { return m_ringBuffer; }
        uint64_t ringBufferSize() const { return kRingBufferSize; }

        StagingBuffer* allocateTemporaryBuffer(uint64_t size);
        void releaseTemporaryBuffer(StagingBuffer* buffer);

        uint32_t getActiveTemporaryBufferCount() const;

        void cleanup();

        bool isInitialized() const { return m_initialized; }

    private:
        RHIRenderer* m_renderer = nullptr;
        bool m_initialized = false;

        static constexpr uint64_t kRingBufferSize = 512 * 1024 * 1024;
        static constexpr uint32_t kMaxTemporaryBuffers = 16;

        BufferPtr m_ringBufferHandle;
        rhi::RHIBuffer* m_ringBuffer = nullptr;
        uint8_t* m_ringBufferMapped = nullptr;

        std::array<std::unique_ptr<StagingBuffer>, kMaxTemporaryBuffers> m_temporaryBuffers;
        mutable std::mutex m_temporaryMutex;
    };
}
