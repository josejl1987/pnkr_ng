#include "pnkr/core/TaskSystem.hpp"
#include "pnkr/core/logger.hpp"

namespace pnkr::core
{
    std::unique_ptr<enki::TaskScheduler> TaskSystem::s_scheduler;

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

        s_scheduler = std::make_unique<enki::TaskScheduler>();
        s_scheduler->Initialize(config.numThreads);
        Logger::info("TaskSystem initialized with {} threads.", s_scheduler->GetNumTaskThreads());
    }

    void TaskSystem::shutdown()
    {
        if (s_scheduler)
        {
            s_scheduler->WaitforAllAndShutdown();
            s_scheduler.reset();
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

    void TaskSystem::launchPinnedTask(enki::IPinnedTask* task, uint32_t threadNum)
    {
      if (!s_scheduler || (task == nullptr)) {
        return;
      }

        task->threadNum = threadNum;
        s_scheduler->AddPinnedTask(task);
    }
}
