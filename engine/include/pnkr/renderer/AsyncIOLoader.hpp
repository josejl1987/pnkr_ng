#pragma once

#include "pnkr/renderer/AsyncLoaderTypes.hpp"
#include <memory>
#include <vector>
#include <mutex>

namespace enki { class ITaskSet; }

namespace pnkr::renderer {

class ResourceRequestManager;
class RHIRenderer;

class AsyncIOLoader : public std::enable_shared_from_this<AsyncIOLoader> {
public:
    AsyncIOLoader(RHIRenderer& renderer, ResourceRequestManager& requestManager);
    ~AsyncIOLoader();

    // Trigger file loading tasks for pending requests in the manager
    void scheduleRequests();

    // Clean up completed tasks
    void cleanupTasks();
    
    // For waiting on shutdown
    void waitAll();

private:
    void processFileRequest(const LoadRequest& req);

    struct FileLoadTask;

    RHIRenderer* m_renderer = nullptr;
    ResourceRequestManager* m_requestManager = nullptr;

    std::vector<std::unique_ptr<FileLoadTask>> m_loadingTasks;
    mutable std::mutex m_taskMutex;
    
    static constexpr uint32_t kMaxConcurrentFileLoads = 32;
};

} // namespace pnkr::renderer
