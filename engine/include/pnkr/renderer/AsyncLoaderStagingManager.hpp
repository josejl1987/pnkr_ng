#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <array>
#include <condition_variable>

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

    struct RingBufferPage
    {
        uint64_t lastBatchId = 0;
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

        struct Allocation {
            uint64_t offset = 0;
            uint8_t* systemPtr = nullptr;
            rhi::RHIBuffer* buffer = nullptr;
            bool isTemporary = false;
            StagingBuffer* tempHandle = nullptr;
            uint64_t batchId = 0;
        };

        [[nodiscard]] uint64_t beginBatch();
        Allocation reserve(uint64_t size, uint64_t batchId);
        void markPages(uint64_t offset, uint64_t size, uint64_t batchId);
        void notifyBatchComplete(uint64_t batchId);
        
        void releaseTemporaryBuffer(StagingBuffer* buffer);
        StagingBuffer* allocateTemporaryBuffer(uint64_t size);

        uint32_t getActiveTemporaryBufferCount() const;

        void cleanup();

        bool isInitialized() const { return m_initialized; }

    private:
        bool waitForPages(uint32_t startPage, uint32_t endPage, uint64_t currentBatchId);

        RHIRenderer* m_renderer = nullptr;
        bool m_initialized = false;

        static constexpr uint64_t kRingBufferSize = 512 * 1024 * 1024;
        static constexpr uint64_t kPageSize = 2 * 1024 * 1024;
        static constexpr uint32_t kTotalPages = kRingBufferSize / kPageSize;
        static constexpr uint32_t kMaxTemporaryBuffers = 16;

        BufferPtr m_ringBufferHandle;
        rhi::RHIBuffer* m_ringBuffer = nullptr;
        uint8_t* m_ringBufferMapped = nullptr;

        std::vector<RingBufferPage> m_pages;
        uint64_t m_head = 0;

        std::atomic<uint64_t> m_nextBatchId{1};
        std::atomic<uint64_t> m_completedBatchId{0};
        
        std::mutex m_batchMutex;
        std::condition_variable m_batchCv;

        std::array<std::unique_ptr<StagingBuffer>, kMaxTemporaryBuffers> m_temporaryBuffers;
        mutable std::mutex m_temporaryMutex;
        mutable std::mutex m_ringMutex;
    };
}

