#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::core
{
    std::unique_ptr<enki::TaskScheduler> TaskSystem::s_scheduler;
    std::unique_ptr<enki::TaskScheduler> TaskSystem::s_ioScheduler;

    void TaskSystem::init()
    {
        init(Config{});
    }

    void TaskSystem::init(const Config& config)
    {
        if (s_scheduler)
        {
            return;
        }

        uint32_t threads = config.numThreads;
        if (threads == 0) {
            uint32_t cores = std::thread::hardware_concurrency();
            // Reserve cores for Main, Render, and OS/Driver (4 total)
            threads = cores > 4 ? cores - 4 : 4;
        }

        s_scheduler = std::make_unique<enki::TaskScheduler>();
        s_scheduler->Initialize(threads);
        
        s_ioScheduler = std::make_unique<enki::TaskScheduler>();
        s_ioScheduler->Initialize(config.numIoThreads);

        Logger::info("TaskSystem initialized: {} compute threads, {} I/O threads.", 
                     s_scheduler->GetNumTaskThreads(), 
                     s_ioScheduler->GetNumTaskThreads());
    }

    void TaskSystem::shutdown()
    {
        if (s_scheduler)
        {
            s_scheduler->WaitforAllAndShutdown();
            s_scheduler.reset();
        }
        if (s_ioScheduler)
        {
            s_ioScheduler->WaitforAllAndShutdown();
            s_ioScheduler.reset();
        }
    }

    bool TaskSystem::isInitialized()
    {
        return s_scheduler != nullptr;
    }

    enki::TaskScheduler& TaskSystem::scheduler()
    {
        return *s_scheduler;
    }

    enki::TaskScheduler& TaskSystem::ioScheduler()
    {
        return *s_ioScheduler;
    }

    void TaskSystem::launchPinnedTask(enki::IPinnedTask* task, uint32_t threadNum)
    {
      if (!s_scheduler || (task == nullptr)) {
        return;
      }

        task->threadNum = threadNum;
        s_scheduler->AddPinnedTask(task);
    }
}
