#include "pnkr/renderer/AsyncLoader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "rhi/vulkan/vulkan_texture.hpp"
#include "rhi/vulkan/vulkan_utils.hpp"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <imgui.h>
#include <utility>

namespace pnkr::renderer
{
    namespace
    {
        constexpr uint64_t K_MAX_UPLOAD_BYTES_PER_FRAME =
            static_cast<const uint64_t>(512 * 1024 * 1024);
        constexpr uint32_t K_MAX_UPLOAD_JOBS_PER_FRAME = 5;
    }

    void
    AsyncLoader::FileLoadTask::ExecuteRange(enki::TaskSetPartition ,
                                            uint32_t ) {
      PNKR_PROFILE_SCOPE("AsyncFileLoad");
      core::Logger::restoreScopes(scopeSnapshot);
      std::string tagCopy = req.path;
      PNKR_PROFILE_TAG(tagCopy.c_str());

      try {
        loader->processFileRequest(req);
      } catch (const std::exception &e) {
        core::Logger::Asset.error(
            "AsyncLoader: Exception during load of {}: {}", req.path, e.what());
      } catch (...) {
        core::Logger::Asset.error(
            "AsyncLoader: Unknown exception during load of {}. This could be a "
            "memory corruption or a non-standard exception.",
            req.path);
      }
    }

    AsyncLoader::AsyncLoader(RHIRenderer& renderer) : m_renderer(&renderer)
    {
        m_stagingManager = std::make_unique<AsyncLoaderStagingManager>(m_renderer);

        rhi::CommandPoolDescriptor poolDesc{};
        poolDesc.queueFamilyIndex = m_renderer->device()->transferQueueFamily();
        poolDesc.flags = rhi::CommandPoolFlags::ResetCommandBuffer;
        m_transferCommandPool = m_renderer->device()->createCommandPool(poolDesc);

        rhi::CommandPoolDescriptor graphicsPoolDesc{};
        graphicsPoolDesc.queueFamilyIndex = m_renderer->device()->graphicsQueueFamily();
        graphicsPoolDesc.flags = rhi::CommandPoolFlags::ResetCommandBuffer;
        m_graphicsCommandPool = m_renderer->device()->createCommandPool(graphicsPoolDesc);

        for (uint32_t i = 0; i < kInFlight; ++i) {
            m_transferCmd[i] = m_renderer->device()->createCommandList(m_transferCommandPool.get());
            m_graphicsCmd[i] = m_renderer->device()->createCommandList(m_graphicsCommandPool.get());
            m_transferFence[i] = m_renderer->device()->createFence(false);
            m_graphicsFence[i] = m_renderer->device()->createFence(false);
            m_slotBusy[i] = false;
        }
        m_submitCursor = 0;

        m_running = true;
        m_transferThread = std::jthread(&AsyncLoader::transferLoop, this);

        m_metrics.lastBandwidthUpdate = std::chrono::steady_clock::now();

        m_initialized = true;
    }

    AsyncLoader::~AsyncLoader() noexcept
    {
      if (!m_initialized) {
        return;
      }

        m_running = false;
        m_transferCv.notify_all();
        if (m_transferThread.joinable()) {
            m_transferThread.join();
        }

        if (core::TaskSystem::isInitialized()) {
            std::vector<std::unique_ptr<FileLoadTask>> tasks;
            {
                std::scoped_lock lock(m_taskMutex);
                tasks = std::move(m_loadingTasks);
            }

            for (auto& t : tasks) {
                core::TaskSystem::scheduler().WaitforTask(t.get());
            }
        }

        if ((m_renderer != nullptr) && (m_renderer->device() != nullptr)) {
          m_renderer->device()->waitIdle();
        }

        {
            while (m_uploadQueue.try_dequeue().has_value()) {  }
            while (m_highPriorityQueue.try_dequeue().has_value()) {  }
            while (m_creationQueue.try_dequeue().has_value()) { }
            while (m_pendingFinalization.try_dequeue().has_value()) { }
        }

        for (auto& b : m_inFlightBatches) {
            for (auto& req : b.jobs) {
                KTXUtils::destroy(req.textureData);
            }
            b.jobs.clear();
            if (b.tempStaging != nullptr) {
              m_stagingManager->releaseTemporaryBuffer(b.tempStaging);
              b.tempStaging = nullptr;
            }
        }

        processDeletionQueue();

        m_stagingManager.reset();
    }

    void AsyncLoader::requestTexture(const std::string& path, TextureHandle handle, bool srgb, LoadPriority priority, uint32_t baseMip)
    {
        PNKR_LOG_SCOPE(std::format("AsyncLoader::Request[{}]", path));
        if (!m_initialized) {
          return;
        }

        m_pendingFileCount.fetch_add(1, std::memory_order_relaxed);

        {
            std::scoped_lock lock(m_taskMutex);
            const double currentTime = ImGui::GetTime();
            m_pendingFileRequests.push_back({.path = path,
                                             .targetHandle = handle,
                                             .srgb = srgb,
                                             .priority = priority,
                                             .baseMip = baseMip,
                                             .timestampStart = currentTime});
        }
    }

    bool AsyncLoader::isValidHandle(TextureHandle handle) const
    {
      if ((m_renderer == nullptr) || handle == INVALID_TEXTURE_HANDLE) {
        return false;
      }
        auto* texture = m_renderer->getTexture(handle);
        return texture != nullptr;
    }

    void AsyncLoader::processFileRequest(const LoadRequest& req)
    {
        PNKR_LOG_SCOPE(std::format("AsyncLoader::ProcessFile[{}]", req.path));
        UploadRequest uploadReq{};
        uploadReq.req = req;
        uploadReq.state.baseMip = req.baseMip;
        uploadReq.state.direction = UploadDirection::LowToHighRes;

        TextureLoadResult result = TextureStreamer::loadTexture(req.path, req.srgb, req.baseMip);

        if (result.success) {
            uploadReq.textureData = std::move(result.textureData);
            uploadReq.isRawImage = result.isRawImage;
            uploadReq.totalSize = result.totalSize;
            uploadReq.targetMipLevels = result.targetMipLevels;

            uploadReq.state.currentLevel = TextureStreamer::getInitialMipLevel(
                uploadReq.textureData, uploadReq.state.baseMip, uploadReq.state.direction);
        } else {
             m_metrics.failedLoads.fetch_add(1, std::memory_order_relaxed);
             uploadReq.totalSize = 0;
        }

        if (req.priority == LoadPriority::Immediate || req.priority == LoadPriority::High) {
            core::Logger::Asset.debug("AsyncLoader: Enqueue Creation HighPriority '{}' ({} bytes)", req.path, uploadReq.totalSize);

            m_creationQueue.enqueue(std::move(uploadReq));
        } else {
            core::Logger::Asset.debug("AsyncLoader: Enqueue Creation '{}' ({} bytes)", req.path, uploadReq.totalSize);
            m_creationQueue.enqueue(std::move(uploadReq));
        }

    }

    bool AsyncLoader::fitInRingBuffer(uint64_t size) {
      return size < kLargeAssetThreshold;
    }

    void AsyncLoader::releaseTemporaryStagingBuffer(StagingBuffer* staging)
    {
        m_stagingManager->releaseTemporaryBuffer(staging);
    }

    void AsyncLoader::processDeletionQueue()
    {
        std::unique_lock<std::mutex> lock(m_deletionMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }

        while (!m_deletionQueue.empty()) {
            auto& item = m_deletionQueue.front();

            if (m_transferFence[item.fenceSlot]->isSignaled()) {
                m_renderer->destroyBuffer(item.bufferHandle);
                m_deletionQueue.pop();
            } else {
                break;
            }
        }
    }

    void AsyncLoader::syncToGPU()
    {
        PNKR_PROFILE_FUNCTION();
        if (!m_initialized) {
          return;
        }

        {
            std::scoped_lock lock(m_taskMutex);
            std::erase_if(m_loadingTasks, [](const auto& t){ return t->GetIsComplete(); });

            while (m_loadingTasks.size() < kMaxConcurrentFileLoads && !m_pendingFileRequests.empty()) {
                auto req = m_pendingFileRequests.front();
                m_pendingFileRequests.pop_front();
                m_pendingFileCount.fetch_sub(1, std::memory_order_relaxed);

                auto task = std::make_unique<FileLoadTask>();
                task->loader = this;
                task->req = req;
                task->m_SetSize = 1;
                task->scopeSnapshot = core::Logger::captureScopes();

                core::TaskSystem::scheduler().AddTaskSetToPipe(task.get());
                m_loadingTasks.push_back(std::move(task));
            }
        }

        {
            std::optional<UploadRequest> optReq;
            int limit = 50;
            bool enqueuedAny = false;
            while (limit-- > 0 && (optReq = m_creationQueue.try_dequeue())) {
                UploadRequest& req = *optReq;

                if (req.totalSize > 0) {
                     rhi::TextureDescriptor desc{};
                    desc.extent = req.textureData.extent;
                    desc.format = req.textureData.format;
                    desc.mipLevels = req.targetMipLevels;
                    desc.arrayLayers = req.textureData.arrayLayers;
                    desc.type = req.textureData.type;
                    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
                    desc.skipBindless = true;
                    desc.debugName = req.req.path + "_intermediate";

                    req.intermediateTexture = m_renderer->createTexture(desc);

                    if (req.intermediateTexture.isValid()) {
                        req.scopeSnapshot = core::Logger::captureScopes();
                        if (req.req.priority == LoadPriority::Immediate || req.req.priority == LoadPriority::High) {
                            m_highPriorityQueue.enqueue(std::move(req));
                        } else {
                            m_uploadQueueSize.fetch_add(1, std::memory_order_relaxed);
                            m_uploadQueue.enqueue(std::move(req));
                        }
                        enqueuedAny = true;
                    } else {
                        core::Logger::Asset.error("[AsyncLoader] Failed to create texture resource for {}", req.req.path);
                        KTXUtils::destroy(req.textureData);
                    }
                } else {
                }
            }
            if (enqueuedAny) {
                 m_transferCv.notify_one();
            }
        }

        {
            std::optional<UploadRequest> optReq;
            while ((optReq = m_pendingFinalization.try_dequeue())) {
                UploadRequest& req = *optReq;

                double currentTime = ImGui::GetTime();
                double durationMs = (currentTime - req.req.timestampStart) * 1000.0;
                core::Logger::Asset.debug("Upload complete '{}' ({:.2f} ms)", req.req.path, durationMs);
                PNKR_TRACY_PLOT("AsyncLoader/LatencyMs", (int64_t)durationMs);

                if (req.intermediateTexture.isValid()) {
                    m_renderer->replaceTexture(req.req.targetHandle, req.intermediateTexture);
                }

                if (req.needsMipmapGeneration && req.intermediateTexture.isValid()) {

                }

                KTXUtils::destroy(req.textureData);

                {
                    std::scoped_lock cLock(m_completedMutex);
                    m_completedTextures.push_back(req.req.targetHandle);
                }

                m_metrics.texturesCompletedTotal.fetch_add(1, std::memory_order_relaxed);
                m_metrics.texturesThisFrame.fetch_add(1, std::memory_order_relaxed);

                uint32_t lIdx = m_metrics.latencyWriteIndex.fetch_add(1) %
                                pnkr::renderer::AsyncLoader::StreamingMetrics::
                                    kLatencySamples;
                m_metrics.latencyHistory[lIdx] = durationMs;
            }
        }

        PNKR_TRACY_PLOT("AsyncLoader/QueueSize", (int64_t)m_uploadQueueSize.load(std::memory_order_relaxed));

        processDeletionQueue();
    }

    void AsyncLoader::transferLoop()
    {
        while (m_running) {

            for (uint32_t slot = 0; slot < kInFlight; ++slot) {
              if (!m_slotBusy[slot]) {
                continue;
              }
              if (!m_transferFence[slot]->isSignaled()) {
                continue;
              }

              (*m_transferFence[slot]).reset();
              m_slotBusy[slot] = false;

              auto &batchJobs = m_inFlightBatches[slot].jobs;

              if (m_inFlightBatches[slot].tempStaging != nullptr) {
                releaseTemporaryStagingBuffer(
                    m_inFlightBatches[slot].tempStaging);
                m_inFlightBatches[slot].tempStaging = nullptr;
              }

                std::vector<UploadRequest> requeuedRequests;

                for (auto& req : batchJobs) {
                    bool done = false;

                    if (req.state.direction == UploadDirection::LowToHighRes) {
                        done = (req.state.currentLevel < 0);
                    } else {
                      const uint32_t effectiveMipLevels =
                          std::max(1U, req.textureData.mipLevels - req.state.baseMip);
                      done = (std::cmp_greater_equal(req.state.currentLevel,
                                                     effectiveMipLevels));
                    }

                    if (done) {
                        m_pendingFinalization.enqueue(std::move(req));
                    } else {
                        requeuedRequests.push_back(std::move(req));
                    }
                }
                batchJobs.clear();

                for (auto& req : requeuedRequests) {
                    if (req.req.priority == LoadPriority::Immediate || req.req.priority == LoadPriority::High) {
                        m_highPriorityQueue.enqueue(std::move(req));
                    } else {
                        m_uploadQueueSize.fetch_add(1, std::memory_order_relaxed);
                        m_uploadQueue.enqueue(std::move(req));
                    }
                }
            }

            uint32_t slotToUse = kInFlight;
            for (uint32_t attempt = 0; attempt < kInFlight; ++attempt) {
                const uint32_t s = (m_submitCursor + attempt) % kInFlight;
                if (!m_slotBusy[s]) { slotToUse = s; break; }
            }

            if (slotToUse == kInFlight) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            std::optional<UploadRequest> reqOpt;

            reqOpt = m_highPriorityQueue.try_dequeue();

            if (!reqOpt) {
                reqOpt = m_uploadQueue.try_dequeue();
                if (reqOpt) {
                    m_uploadQueueSize.fetch_sub(1, std::memory_order_relaxed);
                }
            }

            if (!reqOpt) {
                reqOpt = m_highPriorityQueue.wait_dequeue_timed(std::chrono::milliseconds(1));

                if (!reqOpt) {
                     reqOpt = m_uploadQueue.try_dequeue();
                     if (reqOpt) {
                        m_uploadQueueSize.fetch_sub(1, std::memory_order_relaxed);
                     }
                }
            }

            if (!reqOpt) {
                continue;
            }

            auto* cmd = m_transferCmd[slotToUse].get();
            auto loopStart = std::chrono::steady_clock::now();

             cmd->begin();

             const uint64_t slotSize = m_stagingManager->ringBufferSize() / kInFlight;
             const uint64_t startOffset = slotSize * slotToUse;
             const uint64_t endOffset = startOffset + slotSize;

            uint64_t currentOffset = startOffset;

            constexpr uint64_t kEndPadding = 4096;

            TemporaryStagingBuffer* tempStaging = nullptr;

            uint64_t bytesThisBatch = 0;
             uint32_t jobsThisBatch = 0;
            auto workStart = std::chrono::steady_clock::now();

            bool firstRequest = true;

            while (true) {
              if (currentOffset + kEndPadding >= endOffset &&
                  (tempStaging == nullptr)) {
                break;
              }

                std::optional<UploadRequest> currentReqOpt;

                if (firstRequest) {
                    currentReqOpt = std::move(reqOpt);
                    firstRequest = false;
                } else {
                    currentReqOpt = m_highPriorityQueue.try_dequeue();
                    if (!currentReqOpt) {
                        currentReqOpt = m_uploadQueue.try_dequeue();
                        if (!currentReqOpt) {
                          break;
                        }
                        m_uploadQueueSize.fetch_sub(1, std::memory_order_relaxed);
                    }
                }

                UploadRequest req = std::move(*currentReqOpt);

                core::Logger::restoreScopes(req.scopeSnapshot);
                PNKR_LOG_SCOPE("GPU_Upload");

                if (!isValidHandle(req.req.targetHandle)) {
                    KTXUtils::destroy(req.textureData);
                    continue;
                }

                if ((tempStaging == nullptr) &&
                    !fitInRingBuffer(req.totalSize)) {
                  tempStaging = m_stagingManager->allocateTemporaryBuffer(req.totalSize);

                  if (tempStaging != nullptr) {
                    currentOffset = 0;
                  } else {
                    core::Logger::Asset.warn(
                        "AsyncLoader: Failed to allocate temp buffer for '{}' "
                        "({} bytes), falling back to RingBuffer. This may "
                        "cause stalling for other assets.",
                        req.req.path, req.totalSize);
                  }
                }

                uint8_t *stagingBase = (tempStaging != nullptr)
                                           ? tempStaging->mapped
                                           : m_stagingManager->ringBufferMapped();
                rhi::RHIBuffer *activeBuffer = (tempStaging != nullptr)
                                                   ? tempStaging->buffer
                                                   : m_stagingManager->ringBuffer();
                uint64_t stagingCapacity =
                    (tempStaging != nullptr) ? tempStaging->size : endOffset;

                uint64_t *offsetRef =
                    (tempStaging != nullptr) ? &currentOffset : &currentOffset;
                uint64_t preJobOffset = currentOffset;

                bool madeProgress = processJob(req, cmd, activeBuffer, std::span<uint8_t>(stagingBase, stagingCapacity), *offsetRef);

                if (!madeProgress) {
                    if (req.req.priority == LoadPriority::Immediate || req.req.priority == LoadPriority::High) {
                        m_highPriorityQueue.enqueue(std::move(req));
                    } else {
                        m_uploadQueueSize.fetch_add(1, std::memory_order_relaxed);
                        m_uploadQueue.enqueue(std::move(req));
                    }
                    break;
                }

                bytesThisBatch += (currentOffset - preJobOffset);
                 jobsThisBatch++;

                m_metrics.bytesUploadedTotal.fetch_add(currentOffset - preJobOffset, std::memory_order_relaxed);
                m_metrics.bytesThisFrame.fetch_add(currentOffset - preJobOffset, std::memory_order_relaxed);

                 m_inFlightBatches[slotToUse].jobs.push_back(std::move(req));

                 if (bytesThisBatch >= K_MAX_UPLOAD_BYTES_PER_FRAME ||
                     jobsThisBatch >= K_MAX_UPLOAD_JOBS_PER_FRAME) {
                   break;
                 }
             }

                bool graphicsWorkNeeded = false;
                auto* graphicsCmd = m_graphicsCmd[slotToUse].get();

                 graphicsCmd->begin();

                std::vector<rhi::RHIMemoryBarrier> acquireBarriers;

                uint32_t transferFamily = m_renderer->device()->transferQueueFamily();
                uint32_t graphicsFamily = m_renderer->device()->graphicsQueueFamily();
                bool differentFamilies = (transferFamily != graphicsFamily);

                for (auto& req : m_inFlightBatches[slotToUse].jobs) {
                    if (req.needsMipmapGeneration && req.intermediateTexture.isValid()) {
                        auto* rhiTex = m_renderer->getTexture(req.intermediateTexture);
                        if (rhiTex != nullptr) {
                          if (differentFamilies) {

                            rhi::RHIMemoryBarrier releaseBarrier{};
                            releaseBarrier.texture = rhiTex;
                            releaseBarrier.srcQueueFamilyIndex = transferFamily;
                            releaseBarrier.dstQueueFamilyIndex = graphicsFamily;
                            releaseBarrier.oldLayout =
                                rhi::ResourceLayout::TransferDst;
                            releaseBarrier.newLayout =
                                rhi::ResourceLayout::TransferDst;
                            releaseBarrier.srcAccessStage =
                                rhi::ShaderStage::Transfer;

                            releaseBarrier.dstAccessStage =
                                rhi::ShaderStage::None;

                            cmd->pipelineBarrier(rhi::ShaderStage::Transfer,
                                                 rhi::ShaderStage::None,
                                                 {releaseBarrier});

                            rhi::RHIMemoryBarrier acquireBarrier =
                                releaseBarrier;
                            acquireBarrier.srcAccessStage =
                                rhi::ShaderStage::None;
                            acquireBarrier.dstAccessStage =
                                rhi::ShaderStage::Transfer;

                            acquireBarriers.push_back(acquireBarrier);
                          }
                          graphicsWorkNeeded = true;
                        }
                    }
                }

                cmd->end();

                auto workEnd = std::chrono::steady_clock::now();
                m_metrics.transferActiveNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(workEnd - workStart).count(), std::memory_order_relaxed);
                m_metrics.batchesSubmitted.fetch_add(1, std::memory_order_relaxed);
                m_inFlightBatches[slotToUse].tempStaging = tempStaging;

                m_renderer->device()->submitCommands(cmd, m_transferFence[slotToUse].get());

                if (graphicsWorkNeeded) {
                    if (differentFamilies && !acquireBarriers.empty()) {
                        graphicsCmd->pipelineBarrier(rhi::ShaderStage::None, rhi::ShaderStage::Transfer, acquireBarriers);
                    }

                    for (auto& req : m_inFlightBatches[slotToUse].jobs) {
                        if (req.needsMipmapGeneration && req.intermediateTexture.isValid()) {
                            m_renderer->getTexture(req.intermediateTexture)->generateMipmaps(graphicsCmd);
                        }
                    }

                    graphicsCmd->end();

                    m_transferFence[slotToUse]->wait();

                    (*m_graphicsFence[slotToUse]).reset();

                    m_renderer->device()->submitCommands(graphicsCmd, m_graphicsFence[slotToUse].get());

                    m_graphicsFence[slotToUse]->wait();
                } else {
                     graphicsCmd->end();
                }

                PNKR_TRACY_PLOT("AsyncLoader/BandwidthMB",
                                (int64_t)(bytesThisBatch /
                                          static_cast<uint64_t>(1024 * 1024)));
                m_slotBusy[slotToUse] = true;
                m_submitCursor = (slotToUse + 1) % kInFlight;

            auto loopEnd = std::chrono::steady_clock::now();
            m_metrics.transferTotalNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(loopEnd - loopStart).count(), std::memory_order_relaxed);
         }
     }

    bool AsyncLoader::processJob(UploadRequest& req, rhi::RHICommandList* cmd, rhi::RHIBuffer* srcBuffer, std::span<uint8_t> stagingBuffer, uint64_t& stagingOffset)
    {
        if (!req.layoutInitialized) {

            if (!req.intermediateTexture.isValid()) {
                core::Logger::Asset.error("[AsyncLoader] Intermediate texture invalid in processJob for {}", req.req.path);
                return true;
            }

            rhi::RHIMemoryBarrier b{};
            b.texture = m_renderer->getTexture(req.intermediateTexture);
            if (b.texture != nullptr) {
              b.srcAccessStage = rhi::ShaderStage::None;
              b.dstAccessStage = rhi::ShaderStage::Transfer;
              b.oldLayout = rhi::ResourceLayout::Undefined;
              b.newLayout = rhi::ResourceLayout::TransferDst;
              cmd->pipelineBarrier(rhi::ShaderStage::None,
                                   rhi::ShaderStage::Transfer, {b});
            }
            req.layoutInitialized = true;
        }

        auto* rhiTex = m_renderer->getTexture(req.intermediateTexture);
        if (rhiTex == nullptr) {
          return true;
        }

        const uint64_t stagingCapacity = stagingBuffer.size();
        const std::string& texPath = req.req.path;

        std::vector<rhi::BufferTextureCopyRegion> copyRegions;
        copyRegions.reserve(32);

        bool collectedAnyRegion = false;
        uint64_t initialStagingOffset = stagingOffset;
        uint64_t previousCopySize = 0;

        std::ifstream fileStream;
        if (!req.isRawImage && (req.textureData.dataPtr == nullptr)) {
          fileStream.open(req.req.path, std::ios::binary);
        }

        while (true) {
            uint64_t alignedStart = (stagingOffset + 15) & ~15;

            auto planOpt = TextureStreamer::planNextCopy(
                req.textureData,
                req.state,
                req.isRawImage,
                stagingCapacity,
                alignedStart,
                rhiTex->format());

            if (!planOpt) {
              const uint32_t effectiveMipLevels =
                  std::max(1U, req.textureData.mipLevels - req.state.baseMip);
              const bool done =
                  (req.state.direction == UploadDirection::LowToHighRes)
                      ? (req.state.currentLevel < 0)
                      : (std::cmp_greater_equal(req.state.currentLevel,
                                                effectiveMipLevels));

              if (!done) {
                core::Logger::Asset.error(
                    "[AsyncLoader] Stall: Buffer full. Deferring: '{}' "
                    "(capacity={}, offset={})",
                    texPath, stagingCapacity, alignedStart);
                if (!collectedAnyRegion &&
                    stagingOffset == initialStagingOffset) {
                  return false;
                }
                break;
              }
                break;
            }

            CopyRegionPlan& plan = *planOpt;
            collectedAnyRegion = true;

            {
                PNKR_PROFILE_SCOPE("Memcpy/Read");
                if (plan.m_sourcePtr != nullptr) {
                  std::copy_n(plan.m_sourcePtr, plan.m_copySize,
                              stagingBuffer.data() +
                                  plan.m_region.bufferOffset);
                } else {
                  if (fileStream.is_open()) {
                    fileStream.seekg(plan.m_fileOffset);
                    fileStream.read(
                        reinterpret_cast<char *>(stagingBuffer.data() +
                                                 plan.m_region.bufferOffset),
                        plan.m_copySize);
                  }
                }
            }

            if (srcBuffer != nullptr) {
              PNKR_PROFILE_SCOPE("Flush");
              srcBuffer->flush(plan.m_region.bufferOffset, plan.m_copySize);
            }

            bool merged = false;
            if (!copyRegions.empty()) {
                auto& prev = copyRegions.back();
                const bool imageContiguous =
                    (prev.textureSubresource.mipLevel ==
                     plan.m_region.textureSubresource.mipLevel) &&
                    (prev.textureSubresource.arrayLayer ==
                     plan.m_region.textureSubresource.arrayLayer) &&
                    (prev.textureOffset.x == plan.m_region.textureOffset.x) &&
                    (prev.textureOffset.z == plan.m_region.textureOffset.z) &&
                    (prev.textureExtent.width ==
                     plan.m_region.textureExtent.width) &&
                    (prev.textureExtent.depth ==
                     plan.m_region.textureExtent.depth) &&
                    (prev.textureOffset.y +
                         static_cast<int32_t>(prev.textureExtent.height) ==
                     plan.m_region.textureOffset.y);

                const bool bufferContiguous =
                    (prev.bufferOffset + previousCopySize ==
                     plan.m_region.bufferOffset);

                if (imageContiguous && bufferContiguous) {
                  prev.textureExtent.height +=
                      plan.m_region.textureExtent.height;
                  previousCopySize += plan.m_copySize;
                  merged = true;
                }
            }

            if (!merged) {
              copyRegions.push_back(plan.m_region);
              previousCopySize = plan.m_copySize;
              stagingOffset = alignedStart;
            }

            stagingOffset += plan.m_copySize;
            req.state.currentRow += plan.m_rowsCopied;

            if (plan.m_isMipFinished) {
              // const uint32_t sourceLevel = req.state.baseMip + req.state.currentLevel;
              // const uint32_t height = std::max(1U, req.textureData.extent.height >> sourceLevel);
              if (req.state.direction == UploadDirection::LowToHighRes) {
                const auto currentMip = static_cast<uint32_t>(req.state.currentLevel);
                const uint32_t levelCount =
                    req.textureData.mipLevels - currentMip;
                if (currentMip > 0) {
                  rhiTex->updateAccessibleMipRange(currentMip, levelCount);
                }
              }
              if (req.isRawImage) {
                core::Logger::Asset.info(
                    "AsyncLoader: Mip finished. Advancing state.");
              }
              
              TextureStreamer::advanceRequestState(req.state, req.textureData);

              bool isHighPriority =
                  (req.req.priority == LoadPriority::High ||
                   req.req.priority == LoadPriority::Immediate);

              if (!isHighPriority) {
                break;
              }
            }
        }

        if (!copyRegions.empty()) {
            cmd->copyBufferToTexture(srcBuffer, rhiTex, copyRegions);
        }

        if (!req.layoutFinalized) {
          const uint32_t effectiveMipLevels =
              std::max(1U, req.textureData.mipLevels - req.state.baseMip);
          const bool done = (req.state.direction == UploadDirection::LowToHighRes)
                                ? (req.state.currentLevel < 0)
                                : (std::cmp_greater_equal(req.state.currentLevel,
                                                          effectiveMipLevels));

          if (done) {
            if (req.isRawImage && req.targetMipLevels > 1) {
              req.needsMipmapGeneration = true;
              req.layoutFinalized = true;
              return true;
            }

            rhiTex->updateAccessibleMipRange(0, req.textureData.mipLevels);

            rhi::RHIMemoryBarrier bEnd{};
            bEnd.texture = rhiTex;
            bEnd.srcAccessStage = rhi::ShaderStage::Transfer;
            bEnd.dstAccessStage = rhi::ShaderStage::Transfer;
            bEnd.oldLayout = rhi::ResourceLayout::TransferDst;
            bEnd.newLayout = rhi::ResourceLayout::ShaderReadOnly;
            cmd->pipelineBarrier(rhi::ShaderStage::Transfer,
                                 rhi::ShaderStage::Transfer, {bEnd});

            if (auto *vkTex =
                    dynamic_cast<rhi::vulkan::VulkanRHITexture *>(rhiTex)) {
              vkTex->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
            }

            req.layoutFinalized = true;
            return true;
          }
        }

        return collectedAnyRegion;
    }

    std::vector<TextureHandle> AsyncLoader::consumeCompletedTextures()
    {
        std::scoped_lock lock(m_completedMutex);
        std::vector<TextureHandle> out;
        out.reserve(m_completedTextures.size());
        out.insert(out.end(), std::make_move_iterator(m_completedTextures.begin()), std::make_move_iterator(m_completedTextures.end()));
        m_completedTextures.clear();
        return out;
    }

    GPUStreamingStatistics AsyncLoader::getStatistics() const
    {
        GPUStreamingStatistics stats;
        stats.queuedAssets = m_pendingFileCount.load(std::memory_order_relaxed);
        stats.queuedAssets += m_uploadQueueSize.load(std::memory_order_relaxed);
        stats.queuedAssets += (uint32_t)m_highPriorityQueue.size_approx();

        uint32_t inFlight = 0;
        for (uint32_t i = 0; i < kInFlight; ++i) {
            if (m_slotBusy[i]) {
                inFlight += (uint32_t)m_inFlightBatches[i].jobs.size();
            }
        }

        {
            std::scoped_lock lock(m_completedMutex);
            inFlight += (uint32_t)m_completedTextures.size();
        }
        stats.inFlightAssets = inFlight;

        stats.stagingTotalBytes = m_stagingManager->ringBufferSize();
        stats.stagingUsedBytes = 0;
        for (uint32_t i = 0; i < kInFlight; ++i) {
          if (m_slotBusy[i]) {
            stats.stagingUsedBytes += (m_stagingManager->ringBufferSize() / kInFlight);
          }
        }

        stats.activeTempBuffers = m_stagingManager->getActiveTemporaryBufferCount();

        stats.bytesUploadedThisFrame = m_metrics.bytesThisFrame.exchange(0, std::memory_order_relaxed);
        stats.bytesUploadedTotal = m_metrics.bytesUploadedTotal.load(std::memory_order_relaxed);
        stats.texturesCompletedThisFrame = m_metrics.texturesThisFrame.exchange(0, std::memory_order_relaxed);
        stats.texturesCompletedTotal = m_metrics.texturesCompletedTotal.load(std::memory_order_relaxed);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_metrics.lastBandwidthUpdate).count();
        if (elapsed >= 500) {
            uint64_t currentBytes = stats.bytesUploadedTotal;
            uint64_t delta = currentBytes - m_metrics.lastBytesCount;
            m_metrics.currentBandwidthMBps = (delta / (1024.0 * 1024.0)) / (elapsed / 1000.0);
            m_metrics.lastBytesCount = currentBytes;
            m_metrics.lastBandwidthUpdate = now;
        }
        stats.uploadBandwidthMBps = m_metrics.currentBandwidthMBps;

        double sum = 0;
        double minL = 1e10;
        double maxL = 0;
        std::vector<double> validSamples;
        for (double s : m_metrics.latencyHistory) {
            if (s > 0) {
                sum += s;
                minL = std::min(minL, s);
                maxL = std::max(maxL, s);
                validSamples.push_back(s);
            }
        }

        if (!validSamples.empty()) {
            stats.avgLatencyMs = sum / validSamples.size();
            stats.minLatencyMs = minL;
            stats.maxLatencyMs = maxL;
            stats.latencySampleCount = (uint32_t)validSamples.size();

            std::ranges::sort(validSamples);
            stats.p95LatencyMs = validSamples[static_cast<size_t>(validSamples.size() * 0.95)];
        }

        stats.streamingPoolBudget = 2ULL * 1024 * 1024 * 1024;

        stats.failedLoads = m_metrics.failedLoads.load(std::memory_order_relaxed);
        stats.batchesSubmittedTotal = m_metrics.batchesSubmitted.load(std::memory_order_relaxed);

        uint64_t activeNs = m_metrics.transferActiveNs.exchange(0, std::memory_order_relaxed);
        uint64_t totalNs = m_metrics.transferTotalNs.exchange(0, std::memory_order_relaxed);
        if (totalNs > 0) {
            stats.transferThreadUtilization = (double)activeNs * 100.0 / totalNs;
        }

        if (stats.batchesSubmittedTotal > 0) {
            stats.avgBatchSizeMB = (stats.bytesUploadedTotal / (1024.0 * 1024.0)) / stats.batchesSubmittedTotal;
        }

        return stats;
    }
}