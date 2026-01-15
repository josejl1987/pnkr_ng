#include "pnkr/renderer/ResourceRequestManager.hpp"
#include <mutex>

namespace pnkr::renderer {

void ResourceRequestManager::addFileRequest(const LoadRequest& req) {
    std::lock_guard<std::mutex> lock(m_fileRequestMutex);
    m_pendingFileRequests.push_back(req);
}

bool ResourceRequestManager::hasPendingFileRequests() const {
    std::lock_guard<std::mutex> lock(m_fileRequestMutex);
    return !m_pendingFileRequests.empty();
}

LoadRequest ResourceRequestManager::popFileRequest() {
    std::lock_guard<std::mutex> lock(m_fileRequestMutex);
    if (m_pendingFileRequests.empty()) {
        return {};
    }
    LoadRequest req = m_pendingFileRequests.front();
    m_pendingFileRequests.pop_front();
    return req;
}

uint32_t ResourceRequestManager::getPendingFileCount() const {
    std::lock_guard<std::mutex> lock(m_fileRequestMutex);
    return (uint32_t)m_pendingFileRequests.size();
}

void ResourceRequestManager::enqueueLoaded(UploadRequest&& req) {
    m_pendingCreationQueue.enqueue(std::move(req));
}

std::optional<UploadRequest> ResourceRequestManager::dequeueLoaded() {
    return m_pendingCreationQueue.try_dequeue();
}

void ResourceRequestManager::enqueueUpload(UploadRequest&& req, bool highPriority) {
    if (highPriority) {
        m_highPriorityQueue.enqueue(std::move(req));
    } else {
        m_uploadQueue.enqueue(std::move(req));
    }
}

std::optional<UploadRequest> ResourceRequestManager::dequeueUpload() {
    std::optional<UploadRequest> req = m_highPriorityQueue.try_dequeue();
    if (req) {
        return req;
    }
    
    req = m_uploadQueue.try_dequeue();
    return req;
}

size_t ResourceRequestManager::getUploadQueueSize() const {
    return m_uploadQueue.size_approx();
}

size_t ResourceRequestManager::getHighPriorityQueueSize() const {
    return m_highPriorityQueue.size_approx();
}

void ResourceRequestManager::enqueueFinalization(UploadRequest&& req) {
    m_pendingFinalization.enqueue(std::move(req));
}

std::optional<UploadRequest> ResourceRequestManager::dequeueFinalization() {
    return m_pendingFinalization.try_dequeue();
}

} // namespace pnkr::renderer
