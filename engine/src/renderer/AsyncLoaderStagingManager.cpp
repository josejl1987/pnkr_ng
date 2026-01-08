#include "pnkr/renderer/AsyncLoaderStagingManager.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::renderer
{
    AsyncLoaderStagingManager::AsyncLoaderStagingManager(RHIRenderer* renderer)
        : m_renderer(renderer)
    {
        rhi::BufferDescriptor desc{};
        desc.size = kRingBufferSize;
        desc.usage = rhi::BufferUsage::TransferSrc;
        desc.memoryUsage = rhi::MemoryUsage::CPUToGPU;
        m_ringBufferHandle = m_renderer->createBuffer("AsyncLoader_Staging_Ring", desc);
        m_ringBuffer = m_renderer->getBuffer(m_ringBufferHandle);

        if (m_ringBuffer != nullptr) {
            m_ringBufferMapped = reinterpret_cast<uint8_t*>(m_ringBuffer->map());
            if (m_ringBufferMapped != nullptr) {
                m_initialized = true;
                core::Logger::Asset.info(
                    "AsyncLoaderStagingManager: Initialized ring buffer ({} MB, {} pages)",
                    kRingBufferSize / (1024 * 1024), kTotalPages);
            } else {
                core::Logger::Asset.error(
                    "AsyncLoaderStagingManager: Failed to map ring buffer memory");
            }
        } else {
            core::Logger::Asset.error(
                "AsyncLoaderStagingManager: Failed to create ring buffer ({} MB). "
                "This may be due to insufficient GPU memory.",
                kRingBufferSize / (1024 * 1024));
        }

        m_pages.resize(kTotalPages);
    }

    AsyncLoaderStagingManager::~AsyncLoaderStagingManager()
    {
        cleanup();
    }

    uint64_t AsyncLoaderStagingManager::beginBatch()
    {
        return m_nextBatchId.fetch_add(1, std::memory_order_relaxed);
    }

    bool AsyncLoaderStagingManager::waitForPages(uint32_t startPage, uint32_t endPage, uint64_t currentBatchId)
    {
        std::unique_lock<std::mutex> lock(m_batchMutex);
        
        for (uint32_t i = startPage; i < endPage && i < kTotalPages; ++i) {
            const uint64_t pageBatchId = m_pages[i].lastBatchId;
            
            if (pageBatchId == 0) {
                continue;
            }
            
            if (pageBatchId >= currentBatchId) {
                core::Logger::Asset.error(
                    "AsyncLoaderStagingManager: Page {} owned by batch {} >= current batch {}. "
                    "This indicates a bug in buffer management.",
                    i, pageBatchId, currentBatchId);
                return false;
            }
            
            while (m_completedBatchId.load(std::memory_order_acquire) < pageBatchId) {
                m_batchCv.wait_for(lock, std::chrono::milliseconds(10));
            }
        }
        
        return true;
    }

    AsyncLoaderStagingManager::Allocation AsyncLoaderStagingManager::reserve(uint64_t size, uint64_t batchId)
    {
        if (size > kRingBufferSize / 2) { 
            auto* temp = allocateTemporaryBuffer(size);
            if (temp) {
                Allocation alloc{};
                alloc.offset = 0;
                alloc.systemPtr = temp->mapped;
                alloc.buffer = temp->buffer;
                alloc.isTemporary = true;
                alloc.tempHandle = temp;
                alloc.batchId = batchId;
                return alloc;
            }
            return {};
        }

        std::unique_lock<std::mutex> lock(m_ringMutex);

        uint64_t start = (m_head + 255) & ~255;
        
        if (start + size > kRingBufferSize) {
            start = 0;
        }

        uint32_t startPage = static_cast<uint32_t>(start / kPageSize);
        uint32_t endPage = static_cast<uint32_t>((start + size + kPageSize - 1) / kPageSize);
        endPage = std::min(endPage, kTotalPages);

        lock.unlock();
        
        if (!waitForPages(startPage, endPage, batchId)) {
            return {};
        }
        
        lock.lock();
        
        m_head = start + size;

        Allocation alloc{};
        alloc.offset = start;
        alloc.systemPtr = m_ringBufferMapped + start;
        alloc.buffer = m_ringBuffer;
        alloc.isTemporary = false;
        alloc.tempHandle = nullptr;
        alloc.batchId = batchId;
        return alloc;
    }

    void AsyncLoaderStagingManager::markPages(uint64_t offset, uint64_t size, uint64_t batchId)
    {
        std::unique_lock<std::mutex> lock(m_ringMutex);
        
        uint32_t startPage = static_cast<uint32_t>(offset / kPageSize);
        uint32_t endPage = static_cast<uint32_t>((offset + size + kPageSize - 1) / kPageSize);

        for (uint32_t i = startPage; i < endPage && i < kTotalPages; ++i) {
            m_pages[i].lastBatchId = batchId;
        }
    }

    void AsyncLoaderStagingManager::notifyBatchComplete(uint64_t batchId)
    {
        uint64_t current = m_completedBatchId.load(std::memory_order_acquire);
        
        while (batchId > current) {
            if (m_completedBatchId.compare_exchange_weak(current, batchId, 
                    std::memory_order_release, std::memory_order_acquire)) {
                break;
            }
        }
        
        m_batchCv.notify_all();
    }

    void AsyncLoaderStagingManager::cleanup()
    {
        if (m_ringBufferHandle.isValid()) {
            if (m_ringBuffer != nullptr && m_ringBufferMapped != nullptr) {
                m_ringBuffer->unmap();
                m_ringBufferMapped = nullptr;
            }
            m_renderer->destroyBuffer(m_ringBufferHandle);
            m_ringBufferHandle.reset();
            m_ringBuffer = nullptr;
        }

        std::scoped_lock lock(m_temporaryMutex);
        for (auto& staging : m_temporaryBuffers) {
            if (staging && staging->handle.isValid()) {
                if (staging->buffer != nullptr && staging->mapped != nullptr) {
                    staging->buffer->unmap();
                    staging->mapped = nullptr;
                }
                m_renderer->destroyBuffer(staging->handle);
                staging.reset();
            }
        }
    }

    StagingBuffer* AsyncLoaderStagingManager::allocateTemporaryBuffer(uint64_t size)
    {
        for (auto& staging : m_temporaryBuffers) {
            bool expected = false;
            if (staging && staging->size >= size &&
                staging->inUse.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                return staging.get();
            }
        }

        std::scoped_lock lock(m_temporaryMutex);

        for (auto& staging : m_temporaryBuffers) {
            bool expected = false;
            if (staging && staging->size >= size &&
                staging->inUse.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                return staging.get();
            }
        }

        for (auto& stagingSlot : m_temporaryBuffers) {
            if (!stagingSlot) {
                auto staging = std::make_unique<StagingBuffer>();
                staging->size = size;

                rhi::BufferDescriptor desc{};
                desc.size = size;
                desc.usage = rhi::BufferUsage::TransferSrc;
                desc.memoryUsage = rhi::MemoryUsage::CPUToGPU;

                staging->handle = m_renderer->createBuffer("AsyncLoader_TemporaryStaging", desc);
                staging->buffer = m_renderer->getBuffer(staging->handle);

                if (staging->buffer != nullptr) {
                    staging->mapped = reinterpret_cast<uint8_t*>(staging->buffer->map());
                    staging->inUse.store(true, std::memory_order_release);

                    StagingBuffer* ptr = staging.get();
                    stagingSlot = std::move(staging);
                    return ptr;
                }
                return nullptr;
            }
        }

        core::Logger::Asset.warn("AsyncLoaderStagingManager: Maximum temporary staging buffers reached");
        return nullptr;
    }

    uint32_t AsyncLoaderStagingManager::getActiveTemporaryBufferCount() const
    {
        uint32_t count = 0;
        std::scoped_lock lock(m_temporaryMutex);
        for (const auto& b : m_temporaryBuffers) {
            if (b && b->inUse.load(std::memory_order_relaxed)) {
                count++;
            }
        }
        return count;
    }

    void AsyncLoaderStagingManager::releaseTemporaryBuffer(StagingBuffer* buffer)
    {
        if (buffer == nullptr) return;
        buffer->inUse.store(false, std::memory_order_release);
    }
}

