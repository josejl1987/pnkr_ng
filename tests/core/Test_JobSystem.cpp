#include "pnkr/core/JobSystem.hpp"
#include <atomic>
#include <doctest/doctest.h>

using namespace pnkr::core;

TEST_SUITE("Job System Validation")
{
    struct MyJobData
    {
        std::atomic<int>* sharedCounter;
        AtomicCounter* internalSync;
    };

    void JobLevel2Func(void* arg)
    {
        auto* data = static_cast<MyJobData*>(arg);
        data->sharedCounter->fetch_add(1);
    }

    void JobLevel1Func(void* arg)
    {
        auto* data = static_cast<MyJobData*>(arg);

        Job subJobs[2];
        subJobs[0] = {JobLevel2Func, data};
        subJobs[1] = {JobLevel2Func, data};

        JobSystem::RunJobs(subJobs, 2, data->internalSync);
        JobSystem::WaitForCounter(data->internalSync, 0);

        data->sharedCounter->fetch_add(10);
    }

    TEST_CASE("Production Pattern Test")
    {
        std::atomic<int> localCounter{0};
        AtomicCounter localSync;
        JobSystem::Init(4, 64);
        MyJobData data = {&localCounter, &localSync};

        Job root = {JobLevel1Func, &data};
        JobSystem::RunJobs(&root, 1, nullptr);

        while (localCounter.load() < 12) { std::this_thread::yield(); }
        CHECK(localCounter.load() == 12);
    }

    using namespace pnkr::core;

    struct DataPacket
    {
        std::atomic<int>* counter;
        AtomicCounter* sync;
        int value;
    };

    TEST_SUITE("Job System Execution Patterns")
    {
        TEST_CASE("The Multi-Waiter Gate")
        {
            JobSystem::Init(4, 64);

            static std::atomic<int> readyCount{0};
            static std::atomic<int> finishCount{0};
            static AtomicCounter gate;
            gate.value.store(1);

            auto waiterJob = [](void* arg)
            {
                readyCount.fetch_add(1);
                JobSystem::WaitForCounter(&gate, 0);
                finishCount.fetch_add(1);
            };

            auto triggerJob = [](void* arg)
            {
            };

            Job waiters[10];
            for (int i = 0; i < 10; ++i) waiters[i] = {waiterJob, nullptr};
            JobSystem::RunJobs(waiters, 10, nullptr);

            while (readyCount.load() < 10) { std::this_thread::yield(); }

            CHECK(finishCount.load() == 0);

            Job trigger = {triggerJob, nullptr};
            JobSystem::RunJobs(&trigger, 1, &gate);

            while (finishCount.load() < 10) { std::this_thread::yield(); }

            CHECK(finishCount.load() == 10);
        }

        TEST_CASE("Recursive Fork-Join (Tree)")
        {
            static std::atomic<int> totalWork{0};

            auto grandChildTask = [](void* arg)
            {
                totalWork.fetch_add(1);
            };

            auto childTask = [](void* arg)
            {
                static AtomicCounter childSync;
                childSync.value.store(0);

                Job gJobs[4];
                for (int i = 0; i < 4; ++i) gJobs[i] = {[](void*) { totalWork.fetch_add(1); }, nullptr};

                JobSystem::RunJobs(gJobs, 4, &childSync);
                JobSystem::WaitForCounter(&childSync, 0);
                totalWork.fetch_add(10);
            };

            auto rootTask = [](void* arg)
            {
                static AtomicCounter rootSync;
                rootSync.value.store(0);

                Job cJobs[4];
                for (int i = 0; i < 4; ++i)
                {
                    cJobs[i] = {(void(*)(void*))arg, nullptr};
                }

                JobSystem::RunJobs(cJobs, 4, &rootSync);
                JobSystem::WaitForCounter(&rootSync, 0);
                totalWork.fetch_add(100);
            };

            Job root = {rootTask, (void*)childTask};
            JobSystem::RunJobs(&root, 1, nullptr);

            while (totalWork.load() < 156) { std::this_thread::yield(); }
            CHECK(totalWork.load() == 156);
        }
    }


    using namespace pnkr::core;

    TEST_SUITE("Job System Stress")
    {
        TEST_CASE("Fiber Pool Exhaustion Safety")
        {
            JobSystem::Init(4, 64);
            static AtomicCounter block;
            block.value.store(1);

            static std::atomic<int> suspendedCount{0};

            auto heavyJob = [](void* arg)
            {
                suspendedCount.fetch_add(1);
                JobSystem::WaitForCounter(&block, 0);
            };

            for (int i = 0; i < 100; ++i)
            {
                Job j = {heavyJob, nullptr};
                JobSystem::RunJobs(&j, 1, nullptr);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            CHECK(suspendedCount.load() <= 64);

            block.value.store(0);
            JobSystem::s_state->readyFibers.enqueue(nullptr);
            block.OnCounterReachedTarget(block.value.load(), JobSystem::s_state);

            while (suspendedCount.load() < 100)
            {
                block.value.store(0);
                block.OnCounterReachedTarget(block.value.load(), JobSystem::s_state);
                std::this_thread::yield();
            }
            CHECK(suspendedCount.load() == 100);
        }

        TEST_CASE("High Frequency Job Churn")
        {
            static std::atomic<int> count{0};
            auto tinyJob = [](void* a) { count.fetch_add(1, std::memory_order_relaxed); };

            constexpr int BATCH_SIZE = 1000;
            constexpr int REPETITIONS = 50;

            for (int r = 0; r < REPETITIONS; ++r)
            {
                std::vector<Job> batch(BATCH_SIZE, {tinyJob, nullptr});
                AtomicCounter sync;
                JobSystem::RunJobs(batch.data(), BATCH_SIZE, &sync);
                JobSystem::WaitForCounter(&sync, 0);
            }

            CHECK(count.load() == (BATCH_SIZE * REPETITIONS));
        }
    }
}
