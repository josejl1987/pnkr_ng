#pragma once

#include <TaskScheduler.h>
#include <memory>
#include "pnkr/core/logger.hpp"

namespace pnkr::core
{
    template<typename Func>
    class ScopedTask : public enki::ITaskSet {
    public:
        ScopedTask(Func&& func)
            : m_func(std::forward<Func>(func))
            , m_snapshot(Logger::captureScopes()) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override {
            Logger::restoreScopes(m_snapshot);
            m_func(range, threadnum);
        }

    private:
        Func m_func;
        ScopeSnapshot m_snapshot;
    };

    class TaskSystem
    {
    public:
        struct Config
        {
            uint32_t numThreads = 0; // 0 = hardware_concurrency() - 4
            uint32_t numIoThreads = 32;
        };

        static void init();
        static void init(const Config& config);
        static void shutdown();
        static bool isInitialized();
        static enki::TaskScheduler& scheduler();
        static enki::TaskScheduler& ioScheduler();

        static void launchPinnedTask(enki::IPinnedTask* task, uint32_t threadNum);

        template<typename Func>
        static void launchScopedTask(Func&& func) {
            auto task = std::make_unique<ScopedTask<Func>>(std::forward<Func>(func));
            scheduler().AddTaskSetToPipe(task.get());
            task.release();
        }

    private:
        static std::unique_ptr<enki::TaskScheduler> s_scheduler;
        static std::unique_ptr<enki::TaskScheduler> s_ioScheduler;
    };
}
