#pragma once

#include "pnkr/renderer/AsyncLoaderTypes.hpp"
#include "pnkr/renderer/ResourceRequestManager.hpp"
#include "pnkr/renderer/AsyncIOLoader.hpp"
#include "pnkr/renderer/GPUTransferQueue.hpp"
#include "pnkr/renderer/AsyncLoaderStagingManager.hpp"
#include "pnkr/renderer/profiling/gpu_profiler.hpp"
#include "pnkr/rhi/rhi_buffer.hpp"
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

namespace pnkr::renderer {
class RHIRenderer;

struct DeletionQueueItem {
  BufferPtr bufferHandle;
  uint32_t fenceSlot;
};



class AsyncLoader {
public:
  explicit AsyncLoader(RHIRenderer &renderer, uint64_t stagingBufferSize = AsyncLoaderStagingManager::kDefaultRingBufferSize);
  ~AsyncLoader() noexcept;

  AsyncLoader(const AsyncLoader &) = delete;
  AsyncLoader &operator=(const AsyncLoader &) = delete;
  AsyncLoader(AsyncLoader &&) = delete;
  AsyncLoader &operator=(AsyncLoader &&) = delete;

  bool isInitialized() const { return m_initialized; }

  void requestTexture(const std::string &path, TextureHandle handle, bool srgb,
                      LoadPriority priority = LoadPriority::Medium,
                      uint32_t baseMip = 0);

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


  RHIRenderer *m_renderer = nullptr;
  bool m_initialized = false;
  
  // Components
  std::unique_ptr<ResourceRequestManager> m_requestManager;
  std::unique_ptr<AsyncLoaderStagingManager> m_stagingManager;
  std::shared_ptr<AsyncIOLoader> m_ioLoader;
  std::unique_ptr<GPUTransferQueue> m_gpuTransfer;
  
  // High-level state that doesn't fit into components perfectly yet, 
  // or needs to be accessible by the facade.
  // Deletion Queue
  // GPUTransferQueue manages fences and resource destruction.
  // AsyncLoader polls completion/errors.
  


  std::vector<TextureHandle> m_completedTextures;
  mutable std::mutex m_completedMutex;

  TextureHandle m_errorTexture = INVALID_TEXTURE_HANDLE;
  TextureHandle m_loadingTexture = INVALID_TEXTURE_HANDLE;
  TextureHandle m_defaultWhite = INVALID_TEXTURE_HANDLE;

  struct StreamingMetrics {
     // We will pull these from components on demand or shadow them
     // simplified to just aggregators
     std::atomic<uint32_t> texturesCompletedTotal{0};
     std::atomic<uint32_t> failedLoads{0};
     
     std::atomic<uint32_t> texturesThisFrameAccumulator{0};
     
     static constexpr uint32_t kLatencySamples = 256;
     std::array<double, kLatencySamples> latencyHistory{};
     std::atomic<uint32_t> latencyWriteIndex{0};

     std::chrono::steady_clock::time_point lastBandwidthUpdate;
     double currentBandwidthMBps = 0.0;
     double averageLatencyMs = 0.0;
     mutable std::mutex metricsMutex;
  } mutable m_metrics;
};

} // namespace pnkr::renderer
