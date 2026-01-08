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
            m_ringBufferMapped = reinterpret_cast<uint8_t *>(m_ringBuffer->map());
        }

        m_initialized = (m_ringBuffer != nullptr);
    }

    AsyncLoaderStagingManager::~AsyncLoaderStagingManager()
    {
        cleanup();
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
                    staging->mapped = reinterpret_cast<uint8_t *>(staging->buffer->map());
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
