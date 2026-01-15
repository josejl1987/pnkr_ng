#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace pnkr::renderer {


struct StagingBuffer {
  BufferPtr handle;
  std::unique_ptr<rhi::RHIBuffer> rawBuffer;
  rhi::RHIBuffer *buffer = nullptr;
  uint8_t *mapped = nullptr;
  uint64_t size = 0;
  std::atomic<bool> inUse{false};
};

struct RingBufferPage {
  uint64_t lastBatchId = 0;
};

class AsyncLoaderStagingManager {
public:

  explicit AsyncLoaderStagingManager(RHIResourceManager *resourceManager, uint64_t ringBufferSize = kDefaultRingBufferSize);
  ~AsyncLoaderStagingManager();

  AsyncLoaderStagingManager(const AsyncLoaderStagingManager &) = delete;
  AsyncLoaderStagingManager &
  operator=(const AsyncLoaderStagingManager &) = delete;

  static constexpr uint64_t kDefaultRingBufferSize = 32 * 1024 * 1024;

  uint8_t *ringBufferMapped() const { return m_ringBufferMapped; }
  rhi::RHIBuffer *ringBuffer() const { return m_ringBuffer; }
  uint64_t ringBufferSize() const { return m_ringBufferSize; }

  struct Allocation {
    uint64_t offset = 0;
    uint8_t *systemPtr = nullptr;
    rhi::RHIBuffer *buffer = nullptr;
    bool isTemporary = false;
    StagingBuffer *tempHandle = nullptr;
    uint64_t batchId = 0;
  };

  [[nodiscard]] uint64_t beginBatch();
  Allocation reserve(uint64_t size, uint64_t batchId, bool wait = true);
  void markPages(uint64_t offset, uint64_t size, uint64_t batchId);
  void notifyBatchComplete(uint64_t batchId);

  void releaseTemporaryBuffer(StagingBuffer *buffer);
  StagingBuffer *allocateTemporaryBuffer(uint64_t size);

  uint32_t getActiveTemporaryBufferCount() const;

  uint64_t getUsedBytes() const;

  void cleanup();

  bool isInitialized() const { return m_initialized; }

private:
  bool waitForPages(uint32_t startPage, uint32_t endPage,
                    uint64_t currentBatchId, bool wait);

  RHIResourceManager *m_resourceManager = nullptr;
  bool m_initialized = false;
  
  static constexpr uint64_t kPageSize = 2 * 1024 * 1024;
  static constexpr uint32_t kMaxTemporaryBuffers = 16;

  // Configuration
  uint64_t m_ringBufferSize = kDefaultRingBufferSize;
  uint32_t m_totalPages = 0;

  BufferPtr m_ringBufferHandle;
  rhi::RHIBuffer *m_ringBuffer = nullptr;
  uint8_t *m_ringBufferMapped = nullptr;

  std::vector<RingBufferPage> m_pages;
  uint64_t m_head = 0;

  std::atomic<uint64_t> m_nextBatchId{1};
  std::atomic<uint64_t> m_completedBatchId{0};

  std::mutex m_batchMutex;
  std::condition_variable m_batchCv;

  std::array<std::unique_ptr<StagingBuffer>, kMaxTemporaryBuffers>
      m_temporaryBuffers;
  mutable std::mutex m_temporaryMutex;
  mutable std::mutex m_ringMutex;
};
} // namespace pnkr::renderer
