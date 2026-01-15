#pragma once

#include "pnkr/core/LockFreeQueue.hpp"
#include "pnkr/renderer/AsyncLoaderTypes.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>
#include <functional>

namespace pnkr::renderer {

class ResourceRequestManager {
public:
    ResourceRequestManager() = default;

    // File Request Management
    void addFileRequest(const LoadRequest& req);
    bool hasPendingFileRequests() const;
    LoadRequest popFileRequest();
    uint32_t getPendingFileCount() const;

    // Upload Queue Management
    void enqueueLoaded(UploadRequest&& req);
    std::optional<UploadRequest> dequeueLoaded();

    void enqueueUpload(UploadRequest&& req, bool highPriority);
    
    // Dequeues from high priority first, then normal
    // Returns std::nullopt if empty
    std::optional<UploadRequest> dequeueUpload();

    // Check sizes (approx)
    size_t getUploadQueueSize() const;
    size_t getHighPriorityQueueSize() const;

    // Finalization Queue (Requests waiting for GPU completion or failed)
    void enqueueFinalization(UploadRequest&& req);
    std::optional<UploadRequest> dequeueFinalization();

    // Statistics / Metrics helper
    // No manual size tracking needed with size_approx()

    using NotifyCallback = std::function<void()>;
    void setUploadNotifyCallback(NotifyCallback cb);

private:
    std::deque<LoadRequest> m_pendingFileRequests;
    mutable std::mutex m_fileRequestMutex;

    NotifyCallback m_notifyCallback;

    core::LockFreeQueue<UploadRequest> m_uploadQueue;
    core::LockFreeQueue<UploadRequest> m_highPriorityQueue;
    
    core::LockFreeQueue<UploadRequest> m_pendingCreationQueue;
    core::LockFreeQueue<UploadRequest> m_pendingFinalization;

};

} // namespace pnkr::renderer
