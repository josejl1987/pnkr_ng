#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include "pnkr/core/LockFreeQueue.hpp"
#include "pnkr/core/Fiber.hpp"

namespace pnkr::core {

    class JobSystem {
    public:
        struct InternalState;

        static void Init(uint32_t numThreads, uint32_t numFibers);
        static void RunJobs(Job* jobs, uint32_t count, struct AtomicCounter* counter = nullptr);
        static void WaitForCounter(struct AtomicCounter* counter, uint32_t value = 0);
        static Fiber* GetCurrentFiber();

        struct InternalState {
            std::vector<std::thread> threads;
            LockFreeQueue<Job> jobQueue;
            LockFreeQueue<Fiber*> idleFibers;
            LockFreeQueue<Fiber*> readyFibers;
            std::atomic<bool> running{true};
        };
        static InternalState* s_state;

    private:
        static void WorkerMain(uint32_t threadIdx);
    };

    struct AtomicCounter {
        std::atomic<uint32_t> value{0};

        struct Waiter {
            Fiber* fiber;
            uint32_t targetValue;
        };

        std::mutex waitMutex;
        std::vector<Waiter> waiters;

        void OnCounterReachedTarget(uint32_t reachedValue, JobSystem::InternalState* state);
        bool AddWaiter(Fiber* f, uint32_t target);
    };
}
