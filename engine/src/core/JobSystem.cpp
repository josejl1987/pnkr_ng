#include "pnkr/core/JobSystem.hpp"
#include <cassert>

namespace pnkr::core {

    enum class FiberStatus { Running, Finished, Suspended };

    JobSystem::InternalState* JobSystem::s_state = nullptr;
    thread_local Fiber* t_CurrentFiber = nullptr;
    thread_local FiberStatus t_FiberStatus = FiberStatus::Running;

    void AtomicCounter::OnCounterReachedTarget(uint32_t reachedValue, JobSystem::InternalState* state) {
        std::lock_guard<std::mutex> lock(waitMutex);
        for (auto it = waiters.begin(); it != waiters.end();) {
            if (it->targetValue == reachedValue) {
                state->readyFibers.enqueue(std::move(it->fiber));
                it = waiters.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool AtomicCounter::AddWaiter(Fiber* f, uint32_t target) {
        std::lock_guard<std::mutex> lock(waitMutex);
        if (value.load(std::memory_order_relaxed) == target) return false;
        waiters.push_back({f, target});
        return true;
    }

    void FiberLoop(Fiber* self) {
        while (true) {
            Job curJob = self->GetJob();
            if (curJob.function) {
                curJob.function(curJob.arg);
            }

            if (curJob.counter) {
                // Use release to ensure job results are visible to the waiter
                uint32_t prev = curJob.counter->value.fetch_sub(1, std::memory_order_release);
                if (prev == 1) {
                    curJob.counter->OnCounterReachedTarget(0, JobSystem::s_state);
                }
            }

            t_FiberStatus = FiberStatus::Finished;
            self->Yield();
        }
    }

    void JobSystem::Init(uint32_t numThreads, uint32_t numFibers) {
        if (s_state) return;
        s_state = new InternalState();

        for (uint32_t i = 0; i < numFibers; ++i) {
            s_state->idleFibers.enqueue(new Fiber(FiberLoop));
        }

        for (uint32_t i = 0; i < numThreads; ++i) {
            s_state->threads.emplace_back(WorkerMain, i);
        }
    }

    void JobSystem::WorkerMain(uint32_t) {
        while (s_state->running.load(std::memory_order_relaxed)) {
            Fiber* fiberToRun = nullptr;

            if (auto opt = s_state->readyFibers.try_dequeue()) {
                fiberToRun = *opt;
            }
            else if (auto optJob = s_state->jobQueue.try_dequeue()) {
                if (auto optFiber = s_state->idleFibers.try_dequeue()) {
                    fiberToRun = *optFiber;
                    fiberToRun->SetJob(*optJob);
                } else {
                    s_state->jobQueue.enqueue(std::move(*optJob));
                    std::this_thread::yield();
                    continue;
                }
            }

            if (fiberToRun) {
                t_CurrentFiber = fiberToRun;
                t_FiberStatus = FiberStatus::Running;

                fiberToRun->Resume();

                if (t_FiberStatus == FiberStatus::Finished) {
                    fiberToRun->SetJob({});
                    s_state->idleFibers.enqueue(std::move(fiberToRun));
                }
                t_CurrentFiber = nullptr;
            } else {
                std::this_thread::yield();
            }
        }
    }

    void JobSystem::RunJobs(Job* jobs, uint32_t count, AtomicCounter* counter) {
        if (counter) counter->value.fetch_add(count, std::memory_order_relaxed);
        for (uint32_t i = 0; i < count; ++i) {
            Job j = jobs[i];
            j.counter = counter;
            s_state->jobQueue.enqueue(std::move(j));
        }
    }

    void JobSystem::WaitForCounter(AtomicCounter* counter, uint32_t value) {
        if (counter->value.load(std::memory_order_acquire) == value) return;

        if (counter->AddWaiter(t_CurrentFiber, value)) {
            t_FiberStatus = FiberStatus::Suspended;
            t_CurrentFiber->Yield();
        }
    }

    Fiber* JobSystem::GetCurrentFiber() { return t_CurrentFiber; }
}
