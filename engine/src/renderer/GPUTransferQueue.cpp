#include "pnkr/renderer/GPUTransferQueue.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/ResourceRequestManager.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include <format>
#include <imgui.h>
#include <fstream>
#include <pnkr/renderer/TextureStreamer.hpp>

namespace pnkr::renderer {

GPUTransferQueue::GPUTransferQueue(RHIRenderer &renderer,
                                   ResourceRequestManager &requestManager,
                                   AsyncLoaderStagingManager &stagingManager)
    : m_renderer(&renderer), m_requestManager(&requestManager),
      m_stagingManager(&stagingManager) {

  rhi::CommandPoolDescriptor poolDesc{};
  poolDesc.queueFamilyIndex = m_renderer->device()->transferQueueFamily();
  poolDesc.flags = rhi::CommandPoolFlags::ResetCommandBuffer;
  m_transferCommandPool = m_renderer->device()->createCommandPool(poolDesc);

  rhi::CommandPoolDescriptor graphicsPoolDesc{};
  graphicsPoolDesc.queueFamilyIndex =
      m_renderer->device()->graphicsQueueFamily();
  graphicsPoolDesc.flags = rhi::CommandPoolFlags::ResetCommandBuffer;
  m_graphicsCommandPool =
      m_renderer->device()->createCommandPool(graphicsPoolDesc);

  for (uint32_t i = 0; i < kInFlight; ++i) {
    m_transferCmd[i] =
        m_renderer->device()->createCommandList(m_transferCommandPool.get());
    m_graphicsCmd[i] =
        m_renderer->device()->createCommandList(m_graphicsCommandPool.get());
    m_transferFence[i] = m_renderer->device()->createFence(false);
    m_graphicsFence[i] = m_renderer->device()->createFence(false);
    m_slotBusy[i] = false;
  }
  m_submitCursor = 0;
}

GPUTransferQueue::~GPUTransferQueue() {
  stopThread();
}

void GPUTransferQueue::startThread() {
  if (!m_running) {
    m_running = true;
    m_transferThread = std::jthread(&GPUTransferQueue::transferLoop, this);
  }
}

void GPUTransferQueue::stopThread() {
  if (!m_running) return;
  core::Logger::Render.trace("GPUTransferQueue::stopThread: Signalling stop");
  m_running = false;
  notifyWorkAvailable();
  if (m_transferThread.joinable()) {
    m_transferThread.join();
  }
  core::Logger::Render.trace("GPUTransferQueue::stopThread: Thread joined");

  // Cleanup remaining resources in flight
  if ((m_renderer != nullptr) && (m_renderer->device() != nullptr)) {
    m_renderer->device()->waitIdle();
  }

  for (auto &b : m_inFlightBatches) {
    for (auto &req : b.jobs) {
      KTXUtils::destroy(req.textureData);
    }
    b.jobs.clear();
    for (auto *staging : b.tempStaging) {
      m_stagingManager->releaseTemporaryBuffer(staging);
    }
    b.tempStaging.clear();
    b.ringBufferRanges.clear();
  }
}

void GPUTransferQueue::notifyWorkAvailable() {
  std::lock_guard<std::mutex> lock(m_transferMutex);
  m_transferCv.notify_one();
}

bool GPUTransferQueue::isValidHandle(TextureHandle handle) const {
  if ((m_renderer == nullptr) || handle == INVALID_TEXTURE_HANDLE) {
    return false;
  }
  auto *texture = m_renderer->resourceManager()->getTexture(handle);
  return texture != nullptr;
}

void GPUTransferQueue::transferLoop() {
  core::Logger::Render.trace("GPUTransferQueue: Thread started");
  while (m_running) {

    for (uint32_t slot = 0; slot < kInFlight; ++slot) {
      if (!m_slotBusy[slot]) {
        continue;
      }
      if (!m_transferFence[slot]->isSignaled()) {
        continue;
      }

      // If we had graphics work (mipmaps), we must also wait for the graphics
      // fence
      bool graphicsPending = false;
      for (const auto &job : m_inFlightBatches[slot].jobs) {
        if (job.needsMipmapGeneration) {
          graphicsPending = true;
          break;
        }
      }

      if (graphicsPending && !m_graphicsFence[slot]->isSignaled()) {
        continue;
      }

      PNKR_PROFILE_SCOPE("TransferLoop_Cleanup");

      (*m_transferFence[slot]).reset();
      if (graphicsPending) {
        (*m_graphicsFence[slot]).reset();
      }

      m_slotBusy[slot] = false;

      auto &batchJobs = m_inFlightBatches[slot].jobs;

      for (auto *staging : m_inFlightBatches[slot].tempStaging) {
        m_stagingManager->releaseTemporaryBuffer(staging);
      }
      m_inFlightBatches[slot].tempStaging.clear();
      m_inFlightBatches[slot].ringBufferRanges.clear();

      std::vector<UploadRequest> requeuedRequests;

      for (auto &req : batchJobs) {
        if (req.layoutFinalized) {
          req.stateMachine.tryTransition(ResourceState::Transferred);
          req.stateMachine.tryTransition(ResourceState::Finalizing);
          m_requestManager->enqueueFinalization(std::move(req));
        } else {
          // Clear staging references before requeue
          req.stagingReferences.clear();
          requeuedRequests.push_back(std::move(req));
        }
      }
      batchJobs.clear();

      // NOW notify batch complete - after we're done with staging data
      m_stagingManager->notifyBatchComplete(m_inFlightBatches[slot].batchId);

      for (auto &req : requeuedRequests) {
        bool highPrio = (req.req.priority == LoadPriority::Immediate ||
                         req.req.priority == LoadPriority::High);
        m_requestManager->enqueueUpload(std::move(req), highPrio);
      }
    }

    uint32_t slotToUse = kInFlight;
    for (uint32_t attempt = 0; attempt < kInFlight; ++attempt) {
      const uint32_t s = (m_submitCursor + attempt) % kInFlight;
      if (!m_slotBusy[s]) {
        slotToUse = s;
        break;
      }
    }

    if (slotToUse == kInFlight) {
      std::unique_lock<std::mutex> lock(m_transferMutex);
      m_transferCv.wait_for(lock, std::chrono::milliseconds(1), [this] {
        if (!m_running)
          return true;
        for (uint32_t s = 0; s < kInFlight; ++s) {
          if (!m_slotBusy[s])
            return true;
          if (m_transferFence[s]->isSignaled())
            return true;
        }
        return false;
      });
      continue;
    }

    // Start a new batch
    m_inFlightBatches[slotToUse].batchId = m_stagingManager->beginBatch();

    std::optional<UploadRequest> reqOpt = m_requestManager->dequeueUpload();

    if (!reqOpt) {
      std::unique_lock<std::mutex> lock(m_transferMutex);
      m_transferCv.wait_for(lock, std::chrono::milliseconds(10), [this] {
        return !m_running || m_requestManager->getHighPriorityQueueSize() > 0 ||
               m_requestManager->getUploadQueueSize() > 0;
      });

      if (!m_running) {
        core::Logger::Render.trace("GPUTransferQueue: Thread exiting loop (shutdown)");
        break;
      }

      reqOpt = m_requestManager->dequeueUpload();
    }

    if (!reqOpt) {
      continue;
    }

    auto *cmd = m_transferCmd[slotToUse].get();
    auto loopStart = std::chrono::steady_clock::now();

    cmd->begin();

    PNKR_PROFILE_SCOPE("GPU_Upload");

    uint64_t bytesThisBatch = 0;
    uint32_t jobsThisBatch = 0;
    auto workStart = std::chrono::steady_clock::now();

    bool firstRequest = true;

    while (true) {
      std::optional<UploadRequest> currentReqOpt;

      if (firstRequest) {
        currentReqOpt = std::move(reqOpt);
        firstRequest = false;
      } else {
        currentReqOpt = m_requestManager->dequeueUpload();
        if (!currentReqOpt) {
          break;
        }
      }

      UploadRequest req = std::move(*currentReqOpt);

      core::Logger::Render.trace("GPUTransferQueue: Processing request '{}'", req.req.path);
      core::Logger::restoreScopes(req.scopeSnapshot);
      PNKR_LOG_SCOPE("GPU_Upload");

      if (!isValidHandle(req.req.targetHandle)) {
        core::Logger::Asset.warn(
            "AsyncLoader: Target handle became invalid for '{}'. "
            "Marking as failed.",
            req.req.path);

        KTXUtils::destroy(req.textureData);

        // Enqueue to finalization to track completion
        UploadRequest failedReq{};
        failedReq.req = req.req;
        failedReq.stateMachine.tryTransition(ResourceState::Failed);
        failedReq.layoutFinalized = true;
        m_requestManager->enqueueFinalization(std::move(failedReq));

        continue;
      }


      auto allocation = m_stagingManager->reserve(
          req.totalSize, m_inFlightBatches[slotToUse].batchId, false);

      if (allocation.systemPtr == nullptr) {
        // This should not happen if we passed wait=true unless timeout or shutdown
        bool highPrio = (req.req.priority == LoadPriority::Immediate ||
                         req.req.priority == LoadPriority::High);
        m_requestManager->enqueueUpload(std::move(req), highPrio);
        break;
      }

      if (allocation.isTemporary) {
        m_inFlightBatches[slotToUse].tempStaging.push_back(
            allocation.tempHandle);
      } else {
        m_inFlightBatches[slotToUse].ringBufferRanges.emplace_back(
            allocation.offset, req.totalSize);
      }

      uint8_t *basePtr = allocation.isTemporary
                             ? allocation.systemPtr
                             : m_stagingManager->ringBufferMapped();
      uint64_t baseCapacity = allocation.isTemporary
                                  ? allocation.tempHandle->size
                                  : m_stagingManager->ringBufferSize();
      uint64_t currentOffset = allocation.offset;

      bool madeProgress =
          processJob(req, cmd, allocation.buffer,
                     std::span<uint8_t>(basePtr, baseCapacity), currentOffset);

      if (!madeProgress) {
        bool highPrio = (req.req.priority == LoadPriority::Immediate ||
                         req.req.priority == LoadPriority::High);
        m_requestManager->enqueueUpload(std::move(req), highPrio);
        break;
      }

      bytesThisBatch += req.totalSize;
      jobsThisBatch++;

      m_bytesUploadedTotal.fetch_add(req.totalSize, std::memory_order_relaxed);
      m_bytesThisFrameAccumulator.fetch_add(req.totalSize,
                                            std::memory_order_relaxed);

      m_inFlightBatches[slotToUse].jobs.push_back(std::move(req));

      if (bytesThisBatch >= kMaxUploadBytesPerFrame ||
          jobsThisBatch >= kMaxUploadJobsPerFrame) {
        break;
      }
    }

    if (m_inFlightBatches[slotToUse].jobs.empty()) {
      cmd->end();
      m_stagingManager->notifyBatchComplete(
          m_inFlightBatches[slotToUse].batchId);
      m_slotBusy[slotToUse] = false;
      continue;
    }

    bool graphicsWorkNeeded = false;
    auto *graphicsCmd = m_graphicsCmd[slotToUse].get();

    graphicsCmd->begin();

    std::vector<rhi::RHIMemoryBarrier> acquireBarriers;

    uint32_t transferFamily = m_renderer->device()->transferQueueFamily();
    uint32_t graphicsFamily = m_renderer->device()->graphicsQueueFamily();

    bool differentFamilies = (transferFamily != graphicsFamily);

    for (auto &req : m_inFlightBatches[slotToUse].jobs) {
      if (req.needsMipmapGeneration && req.intermediateTexture.isValid()) {
        auto *rhiTex = m_renderer->getTexture(req.intermediateTexture);
        if (rhiTex != nullptr) {
          if (differentFamilies) {

            rhi::RHIMemoryBarrier releaseBarrier{};
            releaseBarrier.texture = rhiTex;
            releaseBarrier.srcQueueFamilyIndex = transferFamily;
            releaseBarrier.dstQueueFamilyIndex = graphicsFamily;
            releaseBarrier.oldLayout = rhi::ResourceLayout::TransferDst;
            releaseBarrier.newLayout = rhi::ResourceLayout::TransferDst;
            releaseBarrier.srcAccessStage = rhi::ShaderStage::Transfer;

            releaseBarrier.dstAccessStage = rhi::ShaderStage::None;

            cmd->pipelineBarrier(rhi::ShaderStage::Transfer,
                                 rhi::ShaderStage::None, releaseBarrier);

            rhi::RHIMemoryBarrier acquireBarrier = releaseBarrier;
            acquireBarrier.srcAccessStage = rhi::ShaderStage::None;
            acquireBarrier.dstAccessStage = rhi::ShaderStage::Transfer;

            acquireBarriers.push_back(acquireBarrier);
          }
          graphicsWorkNeeded = true;
        }
      }
    }

    cmd->end();

    auto workEnd = std::chrono::steady_clock::now();
    m_transferActiveNs.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(workEnd -
                                                             workStart)
            .count(),
        std::memory_order_relaxed);
    m_batchesSubmitted.fetch_add(1, std::memory_order_relaxed);

    {
      PNKR_PROFILE_SCOPE("Transfer_Submit");
      m_renderer->device()->submitCommands(cmd,
                                           m_transferFence[slotToUse].get());
    }

    for (const auto &range : m_inFlightBatches[slotToUse].ringBufferRanges) {
      m_stagingManager->markPages(range.first, range.second,
                                  m_inFlightBatches[slotToUse].batchId);
    }

    if (graphicsWorkNeeded) {
      PNKR_PROFILE_SCOPE("Graphics_Submission");
      if (differentFamilies && !acquireBarriers.empty()) {
        graphicsCmd->pipelineBarrier(rhi::ShaderStage::None,
                                     rhi::ShaderStage::Transfer,
                                     acquireBarriers);
      }

      for (auto &req : m_inFlightBatches[slotToUse].jobs) {
        if (req.needsMipmapGeneration && req.intermediateTexture.isValid()) {
          m_renderer->getTexture(req.intermediateTexture)
              ->generateMipmaps(graphicsCmd);
        }
      }

      graphicsCmd->end();

      // Reset and submit graphics commands with the graphics fence.
      // We don't wait here; the next iteration of the loop will check the
      // fences.
      (*m_graphicsFence[slotToUse]).reset();
      m_renderer->device()->submitCommands(graphicsCmd,
                                           m_graphicsFence[slotToUse].get());
    } else {
      graphicsCmd->end();
    }

    PNKR_TRACY_PLOT(
        "AsyncLoader/BatchSizeMB",
        (int64_t)(bytesThisBatch / static_cast<uint64_t>(1024 * 1024)));
    m_slotBusy[slotToUse] = true;
    m_submitCursor = (slotToUse + 1) % kInFlight;

    auto loopEnd = std::chrono::steady_clock::now();
    m_transferTotalNs.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(loopEnd -
                                                             loopStart)
            .count(),
        std::memory_order_relaxed);
  }
}

bool GPUTransferQueue::processJob(UploadRequest &req, rhi::RHICommandList *cmd,
                                  rhi::RHIBuffer *srcBuffer,
                                  std::span<uint8_t> stagingBuffer,
                                  uint64_t &stagingOffset) {
  PNKR_PROFILE_FUNCTION();
  PNKR_PROFILE_TAG(req.req.path.c_str());
  core::Logger::Render.trace("GPUTransferQueue::processJob: {}", req.req.path);

  if (!req.layoutInitialized) {
    if (!req.intermediateTexture.isValid()) {
      // ... (Error handling)
      // Error handling needs to be robust here.
      core::Logger::Asset.error(
          "[AsyncLoader] Intermediate texture invalid in processJob for {}. ",
          req.req.path);
      req.layoutFinalized = true;
      KTXUtils::destroy(req.textureData);
      return true;
    }

    rhi::RHIMemoryBarrier b{};
    b.texture = m_renderer->getTexture(req.intermediateTexture);
    if (b.texture != nullptr) {
      b.srcAccessStage = rhi::ShaderStage::None;
      b.dstAccessStage = rhi::ShaderStage::Transfer;
      b.oldLayout = rhi::ResourceLayout::Undefined;
      b.newLayout = rhi::ResourceLayout::TransferDst;
      cmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::Transfer,
                           b);
    }
    req.layoutInitialized = true;
  }

  auto *rhiTex = m_renderer->getTexture(req.intermediateTexture);
  if (rhiTex == nullptr) {
    return true;
  }

  const uint64_t stagingCapacity = stagingBuffer.size();
  bool collectedAnyRegion = false;
  uint64_t initialStagingOffset = stagingOffset;

  std::ifstream fileStream;

  while (true) {
    uint64_t alignedStart = (stagingOffset + 15) & ~15;

    auto planOpt = TextureStreamer::planNextCopy(
        req.textureData, req.state, req.isRawImage, stagingCapacity,
        alignedStart, rhiTex->format());

    if (!planOpt) {
      // Check completion
      const uint32_t effectiveMipLevels =
          std::max(1U, req.textureData.mipLevels - req.state.baseMip);
      const bool done = (req.state.direction == UploadDirection::LowToHighRes)
                            ? (req.state.currentLevel < 0)
                            : (std::cmp_greater_equal(req.state.currentLevel,
                                                      effectiveMipLevels));

      if (!done) {
        if (!collectedAnyRegion && stagingOffset == initialStagingOffset) {
          return false; // Need more space or something, retry later
        }
        break;
      }
      break; // Done
    }

    CopyRegionPlan &plan = *planOpt;

    // Buffer overflow check
    if (alignedStart + plan.m_copySize > stagingCapacity) {
        core::Logger::Asset.error("AsyncLoader: Staging buffer overflow detected for '{}'. Offset={}, Size={}, Capacity={}", 
            req.req.path, alignedStart, plan.m_copySize, stagingCapacity);
        break;
    }

    collectedAnyRegion = true;

    {
      PNKR_PROFILE_SCOPE("Memcpy/Read");
      if (plan.m_sourcePtr != nullptr) {
        std::copy_n(plan.m_sourcePtr, plan.m_copySize,
                    stagingBuffer.data() + alignedStart);
      } else {
        if (!fileStream.is_open()) {
            fileStream.open(req.req.path, std::ios::binary);
        }
        if (fileStream.is_open()) {
            fileStream.seekg(plan.m_fileOffset);
            fileStream.read(reinterpret_cast<char*>(stagingBuffer.data() + alignedStart), plan.m_copySize);
            if (!fileStream) {
                core::Logger::Asset.error("AsyncLoader: File read failed for '{}' at offset {}", req.req.path, plan.m_fileOffset);
                break;
            }
        } else {
            core::Logger::Asset.error("AsyncLoader: Failed to open file for streaming '{}'", req.req.path);
            break;
        }
      }
    }

    // Record copy command
    rhi::BufferTextureCopyRegion region = plan.m_region;
    region.bufferOffset = alignedStart;

    cmd->copyBufferToTexture(srcBuffer, rhiTex, {region});

    stagingOffset = alignedStart + plan.m_copySize;
    TextureStreamer::advanceRequestState(req.state, req.textureData);
  }

  // Check if done
  const uint32_t effectiveMipLevels =
      std::max(1U, req.textureData.mipLevels - req.state.baseMip);
  const bool done = (req.state.direction == UploadDirection::LowToHighRes)
                        ? (req.state.currentLevel < 0)
                        : (std::cmp_greater_equal(req.state.currentLevel,
                                                  effectiveMipLevels));

  if (done) {
    req.layoutFinalized = true;
    req.needsMipmapGeneration =
        (req.targetMipLevels > 1) &&
        (effectiveMipLevels < req.targetMipLevels); 
  }

  return true; // Made progress or done
}

} // namespace pnkr::renderer
