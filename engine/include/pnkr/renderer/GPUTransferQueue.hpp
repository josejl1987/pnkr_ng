#pragma once

#include "pnkr/renderer/AsyncLoaderTypes.hpp"
#include "pnkr/renderer/AsyncLoaderStagingManager.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_sync.hpp"
#include "pnkr/rhi/rhi_device.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <array>
#include <condition_variable>
#include <span>

namespace pnkr::renderer {

class RHIRenderer;
class ResourceRequestManager;

class GPUTransferQueue {
public:
    GPUTransferQueue(RHIRenderer& renderer, ResourceRequestManager& requestManager, AsyncLoaderStagingManager& stagingManager);
    ~GPUTransferQueue();

    void startThread();
    void stopThread();
    
    // Notify thread that new work is available
    void notifyWorkAvailable();

    // Accessors for metrics
    uint64_t getBytesUploadedTotal() const { return m_bytesUploadedTotal.load(std::memory_order_relaxed); }
    uint32_t getBatchesSubmitted() const { return m_batchesSubmitted.load(std::memory_order_relaxed); }
    uint64_t getTransferActiveNs() const { return m_transferActiveNs.load(std::memory_order_relaxed); }
    uint64_t getTransferTotalNs() const { return m_transferTotalNs.load(std::memory_order_relaxed); }

    // Per frame accumulators (reset by owner)
    uint64_t getAndResetBytesThisFrame() { return m_bytesThisFrameAccumulator.exchange(0, std::memory_order_relaxed); }
    
private:
    void transferLoop();

    // Returns true if made progress, false if should stop/yield
    bool processJob(UploadRequest& req, rhi::RHICommandList* cmd,
                   rhi::RHIBuffer* srcBuffer, std::span<uint8_t> stagingBuffer,
                   uint64_t& stagingOffset);
    
    // Helper to check validity of texture handle
    bool isValidHandle(TextureHandle handle) const;

    RHIRenderer* m_renderer = nullptr;
    ResourceRequestManager* m_requestManager = nullptr;
    AsyncLoaderStagingManager* m_stagingManager = nullptr;

    static constexpr uint32_t kInFlight = 3;
    static constexpr uint64_t kLargeAssetThreshold = 128 * 1024 * 1024;
    static constexpr uint64_t kMaxUploadBytesPerFrame = 128 * 1024 * 1024;
    static constexpr uint32_t kMaxUploadJobsPerFrame = 128;

    struct InFlightBatch {
        std::vector<UploadRequest> jobs;
        std::vector<StagingBuffer*> tempStaging;
        std::vector<std::pair<uint64_t, uint64_t>> ringBufferRanges;
        uint64_t batchId = 0;
    };
    std::array<InFlightBatch, kInFlight> m_inFlightBatches;

    std::unique_ptr<rhi::RHICommandPool> m_transferCommandPool;
    std::array<std::unique_ptr<rhi::RHICommandList>, kInFlight> m_transferCmd;
    std::unique_ptr<rhi::RHICommandPool> m_graphicsCommandPool;
    std::array<std::unique_ptr<rhi::RHICommandList>, kInFlight> m_graphicsCmd;

    std::array<std::unique_ptr<rhi::RHIFence>, kInFlight> m_transferFence;
    std::array<std::unique_ptr<rhi::RHIFence>, kInFlight> m_graphicsFence;
    std::array<bool, kInFlight> m_slotBusy{};
    uint32_t m_submitCursor = 0;

    std::jthread m_transferThread;
    std::atomic<bool> m_running{false};
    std::condition_variable m_transferCv;
    std::mutex m_transferMutex;

    // Metrics
    std::atomic<uint64_t> m_bytesUploadedTotal{0};
    std::atomic<uint32_t> m_batchesSubmitted{0};
    std::atomic<uint64_t> m_transferActiveNs{0};
    std::atomic<uint64_t> m_transferTotalNs{0};
    std::atomic<uint64_t> m_bytesThisFrameAccumulator{0};
};

} // namespace pnkr::renderer
