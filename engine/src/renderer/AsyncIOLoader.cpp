#include "pnkr/renderer/AsyncIOLoader.hpp"
#include "pnkr/core/logger.hpp"
#include "pnkr/core/profiler.hpp"
#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/renderer/ResourceRequestManager.hpp"
#include "pnkr/renderer/TextureStreamer.hpp"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"

namespace pnkr::renderer {

struct AsyncIOLoader::FileLoadTask : enki::ITaskSet {
  std::shared_ptr<AsyncIOLoader> loader;
  LoadRequest req;
  core::ScopeSnapshot scopeSnapshot;

  void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;

  FileLoadTask() = default;
  FileLoadTask(const FileLoadTask &) = delete;
  FileLoadTask &operator=(const FileLoadTask &) = delete;
};

AsyncIOLoader::AsyncIOLoader(RHIRenderer &renderer,
                             ResourceRequestManager &requestManager)
    : m_renderer(&renderer), m_requestManager(&requestManager) {}

AsyncIOLoader::~AsyncIOLoader() {
  waitAll();
}

void AsyncIOLoader::waitAll() {
  if (core::TaskSystem::isInitialized()) {
    std::vector<std::unique_ptr<FileLoadTask>> tasks;
    {
      std::scoped_lock lock(m_taskMutex);
      tasks = std::move(m_loadingTasks);
    }

    for (auto &t : tasks) {
      core::TaskSystem::ioScheduler().WaitforTask(t.get());
    }
  }
}

void AsyncIOLoader::cleanupTasks() {
  std::scoped_lock lock(m_taskMutex);
  std::erase_if(m_loadingTasks,
                [](const auto &t) { return t->GetIsComplete(); });
}

void AsyncIOLoader::scheduleRequests() {
  PNKR_PROFILE_FUNCTION();

  // Clean up finished tasks first to make room
  cleanupTasks();

  std::scoped_lock lock(m_taskMutex);
  while (m_loadingTasks.size() < kMaxConcurrentFileLoads &&
         m_requestManager->hasPendingFileRequests()) {

    // Check pending requests again under lock (though manager is thread safe,
    // we want consistency) Accessing manager without lock is fine for atomic
    // count check. We pop one by one.
    if (!m_requestManager->hasPendingFileRequests())
      break;

    auto req = m_requestManager->popFileRequest();

    auto task = std::make_unique<FileLoadTask>();
    task->loader = shared_from_this();
    task->req = req;
    task->m_SetSize = 1;
    task->scopeSnapshot = core::Logger::captureScopes();

    core::TaskSystem::ioScheduler().AddTaskSetToPipe(task.get());
    m_loadingTasks.push_back(std::move(task));
  }
}

void AsyncIOLoader::FileLoadTask::ExecuteRange(enki::TaskSetPartition,
                                               uint32_t) {
  PNKR_PROFILE_SCOPE("AsyncFileLoad");
  core::Logger::restoreScopes(scopeSnapshot);
  PNKR_PROFILE_TAG(req.path.c_str());

  try {
    loader->processFileRequest(req);
  } catch (const std::exception &e) {
    core::Logger::Asset.error("AsyncLoader: Exception during load of {}: {}",
                              req.path, e.what());
    // Report failure
    UploadRequest failedReq{};
    failedReq.req = req;
    failedReq.stateMachine.tryTransition(ResourceState::Failed);
    failedReq.layoutFinalized = true;
    loader->m_requestManager->enqueueFinalization(std::move(failedReq));
  } catch (...) {
    core::Logger::Asset.error(
        "AsyncLoader: Unknown exception during load of {}.", req.path);
    // Report failure
    UploadRequest failedReq{};
    failedReq.req = req;
    failedReq.stateMachine.tryTransition(ResourceState::Failed);
    failedReq.layoutFinalized = true;
    loader->m_requestManager->enqueueFinalization(std::move(failedReq));
  }
}

void AsyncIOLoader::processFileRequest(const LoadRequest &req) {
  PNKR_LOG_SCOPE(std::format("AsyncLoader::ProcessFile[{}]", req.path));
  PNKR_PROFILE_SCOPE("AsyncLoader::ProcessFileReq");
  UploadRequest uploadReq{};
  uploadReq.stateMachine.tryTransition(ResourceState::Pending);
  uploadReq.req = req;
  uploadReq.state.baseMip = req.baseMip;
  uploadReq.state.direction = UploadDirection::LowToHighRes;

  uploadReq.stateMachine.tryTransition(ResourceState::Loading);
  TextureLoadResult result =
      TextureStreamer::loadTexture(req.path, req.srgb, req.baseMip);

  if (result.success) {
    uploadReq.stateMachine.tryTransition(ResourceState::Decoded);
    uploadReq.textureData = std::move(result.textureData);
    uploadReq.isRawImage = result.isRawImage;
    uploadReq.totalSize = result.totalSize;
    uploadReq.targetMipLevels = result.targetMipLevels;

    uploadReq.state.currentLevel = TextureStreamer::getInitialMipLevel(
        uploadReq.textureData, uploadReq.state.baseMip,
        uploadReq.state.direction);

    rhi::TextureDescriptor desc{};
    desc.extent = uploadReq.textureData.extent;
    desc.format = uploadReq.textureData.format;
    desc.mipLevels = uploadReq.targetMipLevels;
    desc.arrayLayers = uploadReq.textureData.arrayLayers;
    desc.type = uploadReq.textureData.type;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst |
                 rhi::TextureUsage::TransferSrc;
    uploadReq.intermediateDesc = desc;
    uploadReq.isHighPriority = (req.priority == LoadPriority::Immediate ||
                                req.priority == LoadPriority::High);

    m_requestManager->enqueueLoaded(std::move(uploadReq));
    return;
  } else {
    // m_metrics.failedLoads.fetch_add(1, std::memory_order_relaxed); // Metrics
    // can be handled by manager/loader polling invalid requests or failures For
    // now logging is enough, finalizer handles cleanup.
    core::Logger::Asset.error(
        "AsyncLoader: Skipping upload for '{}' due to load failure.", req.path);

    UploadRequest failedReq{};
    failedReq.req = req;
    failedReq.stateMachine.tryTransition(ResourceState::Failed);
    failedReq.layoutFinalized = true;
    m_requestManager->enqueueFinalization(std::move(failedReq));
    return;
  }

  // enqueueLoaded handles the transition now

  // Notify transfer thread (via GPUTransferQueue, but we don't have direct ref
  // here easily properly) Wait, AsyncLoader coordinated this. GPUTransferQueue
  // needs to wake up. We can expose a callback or reference. Or AsyncLoader
  // polls. Ideally requestManager has a "notify" callback or condition variable
  // shared. GPUTransferQueue waits on a CV. ResourceRequestManager could own
  // the CV? Or GPUTransferQueue exposes a `notifyWorkAvailable()`.
  // AsyncIOLoader doesn't know about GPUTransferQueue.
  // The facade (AsyncLoader) can wire this up, but here we are deep in a task.
  // Maybe we just enqueue and let the transfer thread wake up periodically or
  // handle CV in Manager.

  // Notification is handled by ResourceRequestManager via callback to GPUTransferQueue
  // when the request is eventually moved to the upload queue.
}

} // namespace pnkr::renderer
