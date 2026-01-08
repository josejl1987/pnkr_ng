#pragma once

#include "pnkr/core/Handle.h"
#include "pnkr/core/LockFreeQueue.hpp"
#include <array>
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include "pnkr/rhi/rhi_command_buffer.hpp"
#include "pnkr/rhi/rhi_sync.hpp"
#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/AsyncLoaderStagingManager.hpp"
#include "pnkr/renderer/TextureStreamer.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>
#include <queue>
#include <type_traits>

#include "profiling/gpu_profiler.hpp"

namespace pnkr::renderer
{
    class RHIRenderer;

    namespace rhi { struct RHICommandPool; }

    using LoadPriority = assets::LoadPriority;

    struct LoadRequest
    {
        std::string path;
        TextureHandle targetHandle;
        bool srgb = true;
        LoadPriority priority = LoadPriority::Medium;
        uint32_t baseMip = 0;
        double timestampStart = 0.0;
    };

    struct UploadRequest
    {
        LoadRequest req;
        KTXTextureData textureData;
        bool isRawImage = false;
        uint64_t totalSize = 0;
        uint32_t targetMipLevels = 0;

        StreamRequestState state;

        bool layoutInitialized = false;
        bool layoutFinalized = false;
        bool needsMipmapGeneration = false;

        core::ScopeSnapshot scopeSnapshot;

        TexturePtr intermediateTexture;
    };

    using TemporaryStagingBuffer = StagingBuffer;

    struct DeletionQueueItem
    {
        BufferPtr bufferHandle;
        uint32_t fenceSlot;
    };

    class AsyncLoader
    {
    public:
        explicit AsyncLoader(RHIRenderer& renderer);
        ~AsyncLoader() noexcept;

        AsyncLoader(const AsyncLoader&) = delete;
        AsyncLoader& operator=(const AsyncLoader&) = delete;
        AsyncLoader(AsyncLoader&&) = delete;
        AsyncLoader& operator=(AsyncLoader&&) = delete;

        bool isInitialized() const { return m_initialized; }

        void requestTexture(const std::string& path, TextureHandle handle, bool srgb, LoadPriority priority = LoadPriority::Medium, uint32_t baseMip = 0);

        void syncToGPU();

        std::vector<TextureHandle> consumeCompletedTextures();

        TextureHandle getErrorTexture() const { return m_errorTexture; }
        TextureHandle getLoadingTexture() const { return m_loadingTexture; }
        TextureHandle getDefaultWhite() const { return m_defaultWhite; }

        void setErrorTexture(TextureHandle handle) { m_errorTexture = handle; }
        void setLoadingTexture(TextureHandle handle) { m_loadingTexture = handle; }
        void setDefaultWhite(TextureHandle handle) { m_defaultWhite = handle; }

        bool isValidHandle(TextureHandle handle) const;
        GPUStreamingStatistics getStatistics() const;

    private:
        struct FileLoadTask : enki::ITaskSet
        {
            AsyncLoader* loader = nullptr;
            LoadRequest req;
            core::ScopeSnapshot scopeSnapshot;
            void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

            FileLoadTask() = default;
            FileLoadTask(const FileLoadTask&) = delete;
            FileLoadTask& operator=(const FileLoadTask&) = delete;
            FileLoadTask(FileLoadTask&&) = delete;
            FileLoadTask& operator=(FileLoadTask&&) = delete;
        };

        static_assert(std::has_virtual_destructor_v<enki::ITaskSet>,
                      "enki::ITaskSet must have a virtual destructor");

        void processFileRequest(const LoadRequest& req);

        bool processJob(UploadRequest& req, rhi::RHICommandList* cmd, rhi::RHIBuffer* srcBuffer, std::span<uint8_t> stagingBuffer, uint64_t& stagingOffset);

        static void advanceRequestState(UploadRequest &req, uint32_t mipHeight);

        static bool fitInRingBuffer(uint64_t size);
        void releaseTemporaryStagingBuffer(StagingBuffer* staging);
        void processDeletionQueue();

        bool uploadToStaging(UploadRequest& req, rhi::RHICommandList* cmd, StagingBuffer* tempStaging = nullptr);

        RHIRenderer* m_renderer = nullptr;
        std::unique_ptr<AsyncLoaderStagingManager> m_stagingManager;
        bool m_initialized = false;

        std::vector<std::unique_ptr<FileLoadTask>> m_loadingTasks;
        std::deque<LoadRequest> m_pendingFileRequests;
        std::atomic<uint32_t> m_pendingFileCount{0};
        mutable std::mutex m_taskMutex;
        static constexpr uint32_t kMaxConcurrentFileLoads = 8;

        core::LockFreeQueue<UploadRequest> m_uploadQueue;
        std::atomic<uint32_t> m_uploadQueueSize{0};

        core::LockFreeQueue<UploadRequest> m_highPriorityQueue;
        core::LockFreeQueue<UploadRequest> m_creationQueue;
        core::LockFreeQueue<UploadRequest> m_pendingFinalization;

        static constexpr uint32_t kInFlight = 2;
        struct InFlightBatch {
            std::vector<UploadRequest> jobs;
            TemporaryStagingBuffer* tempStaging = nullptr;
        };
        std::array<InFlightBatch, kInFlight> m_inFlightBatches;

        static constexpr uint64_t kLargeAssetThreshold = 128 * 1024 * 1024;

        std::queue<DeletionQueueItem> m_deletionQueue;
        mutable std::mutex m_deletionMutex;

        std::unique_ptr<rhi::RHICommandPool> m_transferCommandPool;
        std::array<std::unique_ptr<rhi::RHICommandList>, kInFlight> m_transferCmd;

        std::unique_ptr<rhi::RHICommandPool> m_graphicsCommandPool;
        std::array<std::unique_ptr<rhi::RHICommandList>, kInFlight> m_graphicsCmd;

        std::array<std::unique_ptr<rhi::RHIFence>, kInFlight> m_transferFence;
        std::array<std::unique_ptr<rhi::RHIFence>, kInFlight> m_graphicsFence;
        std::array<bool, kInFlight> m_slotBusy{};
        uint32_t m_submitCursor = 0;

        std::vector<TextureHandle> m_completedTextures;
        mutable std::mutex m_completedMutex;

        TextureHandle m_errorTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_loadingTexture = INVALID_TEXTURE_HANDLE;
        TextureHandle m_defaultWhite = INVALID_TEXTURE_HANDLE;

        std::jthread m_transferThread;
        std::atomic<bool> m_running{false};
        std::condition_variable m_transferCv;
        std::mutex m_transferMutex;

        void transferLoop();

        struct StreamingMetrics {
            std::atomic<uint64_t> bytesUploadedTotal{0};
            std::atomic<uint32_t> texturesCompletedTotal{0};
            std::atomic<uint32_t> failedLoads{0};
            std::atomic<uint32_t> batchesSubmitted{0};

            std::atomic<uint64_t> bytesThisFrame{0};
            std::atomic<uint32_t> texturesThisFrame{0};

            static constexpr uint32_t kLatencySamples = 256;
            std::array<double, kLatencySamples> latencyHistory{};
            std::atomic<uint32_t> latencyWriteIndex{0};

            std::atomic<uint64_t> transferActiveNs{0};
            std::atomic<uint64_t> transferTotalNs{0};

            std::chrono::steady_clock::time_point lastBandwidthUpdate;
            uint64_t lastBytesCount = 0;
            double currentBandwidthMBps = 0.0;
        } mutable m_metrics;
    };
}
