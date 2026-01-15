#include "pnkr/renderer/AsyncLoader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include <chrono>

namespace pnkr::renderer {

AsyncLoader::AsyncLoader(RHIRenderer &renderer, uint64_t stagingBufferSize) : m_renderer(&renderer) {
  m_requestManager = std::make_unique<ResourceRequestManager>();

  m_stagingManager = std::make_unique<AsyncLoaderStagingManager>(
      m_renderer->resourceManager(), stagingBufferSize);
  if (!m_stagingManager->isInitialized()) {
    core::Logger::Asset.error(
        "AsyncLoader: Staging manager initialization failed.");
    return;
  }

  m_ioLoader = std::make_shared<AsyncIOLoader>(*m_renderer, *m_requestManager);
  m_gpuTransfer = std::make_unique<GPUTransferQueue>(
      *m_renderer, *m_requestManager, *m_stagingManager);

  m_gpuTransfer->startThread();
  m_metrics.lastBandwidthUpdate = std::chrono::steady_clock::now();
  m_initialized = true;
}

AsyncLoader::~AsyncLoader() noexcept {
  if (!m_initialized) {
    return;
  }

  // Shutdown order matters
  if (m_gpuTransfer) {
    m_gpuTransfer->stopThread();
  }
  if (m_ioLoader) {
    m_ioLoader->waitAll();
  }

  // Process final cleanup
  {
    while (auto req = m_requestManager->dequeueUpload()) {
      KTXUtils::destroy(req->textureData);
    }
    while (auto req = m_requestManager->dequeueFinalization()) {
      KTXUtils::destroy(req->textureData);
    }
  }

  m_gpuTransfer.reset();
  m_ioLoader.reset();
  m_stagingManager.reset();
  m_requestManager.reset();
}

void AsyncLoader::requestTexture(const std::string &path, TextureHandle handle,
                                 bool srgb, LoadPriority priority,
                                 uint32_t baseMip) {
  PNKR_LOG_SCOPE(std::format("AsyncLoader::Request[{}]", path));
  PNKR_PROFILE_FUNCTION();

  if (!m_initialized) {
    return;
  }

  LoadRequest req{.path = path,
                  .targetHandle = handle,
                  .srgb = srgb,
                  .priority = priority,
                  .baseMip = baseMip,
                  .timestampStart = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()};

  m_requestManager->addFileRequest(req);
}

void AsyncLoader::syncToGPU() {
  PNKR_PROFILE_FUNCTION();
  if (!m_initialized)
    return;

  try {
    // 1. Schedule IO Tasks
    m_ioLoader->scheduleRequests();

    // 2. Process Pending Creations (on Render Thread)
    std::optional<UploadRequest> loadedReq;
    while ((loadedReq = m_requestManager->dequeueLoaded())) {
        PNKR_PROFILE_SCOPE("AsyncLoader::CreateTexture");
        
        // Create the texture resource from the intermediate description

        
        loadedReq->intermediateTexture = m_renderer->createTexture(loadedReq->intermediateDesc);
        if (!loadedReq->intermediateTexture.isValid()) {
             core::Logger::Asset.error("[AsyncLoader] Failed to create texture resource for {}", loadedReq->req.path);
             loadedReq->stateMachine.tryTransition(ResourceState::Failed);
             loadedReq->layoutFinalized = true;
             m_requestManager->enqueueFinalization(std::move(*loadedReq));
             continue;
        }

        loadedReq->stateMachine.tryTransition(ResourceState::Uploading);
        
        if (loadedReq->isHighPriority) {
            core::Logger::Asset.debug("AsyncLoader: Enqueue Upload HighPriority '{}' ({} bytes)", loadedReq->req.path, loadedReq->totalSize);
        } else {
            core::Logger::Asset.debug("AsyncLoader: Enqueue Upload '{}' ({} bytes)", loadedReq->req.path, loadedReq->totalSize);
        }

        m_requestManager->enqueueUpload(std::move(*loadedReq), loadedReq->isHighPriority);
    }

    // 3. Notify Transfer (if needed, though it loops)
    m_gpuTransfer->notifyWorkAvailable(); // Ensure it wakes up if sleeping

    // 4. Process Finalizations (Completed uploads)
    std::optional<UploadRequest> optReq;
    while ((optReq = m_requestManager->dequeueFinalization())) {
      UploadRequest &req = *optReq;

      // If it failed, we still track it here?
      if (req.stateMachine.getCurrentState() == ResourceState::Failed) {
        m_metrics.failedLoads.fetch_add(1, std::memory_order_relaxed);

        // Cleanup intermediate resource if it was created
        if (req.intermediateTexture.isValid()) {
          m_renderer->destroyTexture(req.intermediateTexture);
        }

        // Assign error texture to visual target to indicate failure
        if (m_errorTexture.isValid()) {
          core::Logger::Render.trace("AsyncLoader: Failure path replaceTexture start");
          m_renderer->replaceTexture(req.req.targetHandle, m_errorTexture);
          core::Logger::Render.trace("AsyncLoader: Failure path replaceTexture end");
        }

        core::Logger::Asset.warn("AsyncLoader: Request failed for '{}'",
                                 req.req.path);

        // Clean up data before continuing
        if (req.textureData.dataPtr != nullptr || !req.textureData.ownedData.empty()) {
            KTXUtils::destroy(req.textureData);
        }

        {
          std::scoped_lock cLock(m_completedMutex);
          m_completedTextures.push_back(req.req.targetHandle);
        }
        continue;
      }

      req.stateMachine.tryTransition(ResourceState::Complete);

      double currentTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      double durationMs = (currentTime - req.req.timestampStart) * 1000.0;
      core::Logger::Asset.debug("Upload complete '{}' ({:.2f} ms)", req.req.path,
                                durationMs);
      PNKR_TRACY_PLOT("AsyncLoader/LatencyMs", (int64_t)durationMs);

      if (req.intermediateTexture.isValid()) {
        core::Logger::Render.trace("AsyncLoader: Success path replaceTexture start (intermediate)");
        m_renderer->replaceTexture(req.req.targetHandle, req.intermediateTexture);
        core::Logger::Render.trace("AsyncLoader: Success path replaceTexture end (intermediate)");
      } else if (m_errorTexture.isValid()) {
        core::Logger::Render.trace("AsyncLoader: Success path replaceTexture start (error)");
        m_renderer->replaceTexture(req.req.targetHandle, m_errorTexture);
        core::Logger::Render.trace("AsyncLoader: Success path replaceTexture end (error)");
      }

      if (req.textureData.dataPtr != nullptr || !req.textureData.ownedData.empty()) {
          KTXUtils::destroy(req.textureData);
      }

      {
        std::scoped_lock cLock(m_completedMutex);
        m_completedTextures.push_back(req.req.targetHandle);
      }

      m_metrics.texturesCompletedTotal.fetch_add(1, std::memory_order_relaxed);
      m_metrics.texturesThisFrameAccumulator.fetch_add(1,
                                                       std::memory_order_relaxed);

      uint32_t lIdx = m_metrics.latencyWriteIndex.fetch_add(1) %
                      StreamingMetrics::kLatencySamples;
      m_metrics.latencyHistory[lIdx] = durationMs;
    }

    // 4. Update Metrics from Components
    PNKR_TRACY_PLOT("AsyncLoader/QueueSize",
                    (int64_t)m_requestManager->getUploadQueueSize());

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - m_metrics.lastBandwidthUpdate).count();
    if (elapsed >= 0.5) {
        std::scoped_lock mLock(m_metrics.metricsMutex);
        uint64_t bytes = m_gpuTransfer->getAndResetBytesThisFrame();
        m_metrics.currentBandwidthMBps = (double(bytes) / (1024.0 * 1024.0)) / elapsed;
        
        uint32_t count = m_metrics.texturesThisFrameAccumulator.exchange(0);
        (void)count; // Could be used for per-second counts

        double sum = 0;
        uint32_t samples = 0;
        for (double d : m_metrics.latencyHistory) {
            if (d > 0) {
                sum += d;
                samples++;
            }
        }
        m_metrics.averageLatencyMs = samples > 0 ? (sum / samples) : 0.0;
        m_metrics.lastBandwidthUpdate = now;
    }
  } catch (const std::exception& e) {
    core::Logger::Asset.critical("AsyncLoader::syncToGPU: Caught exception: {}", e.what());
  } catch (...) {
    core::Logger::Asset.critical("AsyncLoader::syncToGPU: Caught unknown exception");
  }
}

std::vector<TextureHandle> AsyncLoader::consumeCompletedTextures() {
  std::scoped_lock lock(m_completedMutex);
  std::vector<TextureHandle> result = std::move(m_completedTextures);
  m_completedTextures.clear();
  return result;
}

bool AsyncLoader::isValidHandle(TextureHandle handle) const {
  if ((m_renderer == nullptr) || handle == INVALID_TEXTURE_HANDLE) {
    return false;
  }
  auto *texture = m_renderer->resourceManager()->getTexture(handle);
  return texture != nullptr;
}

GPUStreamingStatistics AsyncLoader::getStatistics() const {
  std::scoped_lock lock(m_metrics.metricsMutex);
  // Aggregate from components
  GPUStreamingStatistics stats{};
  stats.bytesUploadedTotal = m_gpuTransfer->getBytesUploadedTotal();
  stats.queuedAssets = m_requestManager->getPendingFileCount();
  stats.inFlightAssets = m_requestManager->getUploadQueueSize() +
                         m_requestManager->getHighPriorityQueueSize();
  stats.uploadBandwidthMBps = m_metrics.currentBandwidthMBps;
  stats.avgLatencyMs = m_metrics.averageLatencyMs;
  stats.failedLoads = m_metrics.failedLoads.load(std::memory_order_relaxed);
  stats.texturesCompletedTotal = m_metrics.texturesCompletedTotal.load(std::memory_order_relaxed);
  stats.texturesCompletedThisFrame = m_metrics.texturesThisFrameAccumulator.load(std::memory_order_relaxed);
  
  stats.stagingTotalBytes = m_stagingManager->ringBufferSize();
  stats.stagingUsedBytes = m_stagingManager->getUsedBytes();
  stats.activeTempBuffers = m_stagingManager->getActiveTemporaryBufferCount();
  
  // Map ring buffer to streaming pool for UI visibility
  stats.streamingPoolBudget = stats.stagingTotalBytes;
  stats.streamingPoolUsed = stats.stagingUsedBytes;
  stats.poolUtilizationPercent = (stats.streamingPoolBudget > 0) ? (double(stats.streamingPoolUsed) / stats.streamingPoolBudget) * 100.0 : 0.0;
  
  return stats;
}

} // namespace pnkr::renderer
