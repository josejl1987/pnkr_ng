#pragma once

#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>
#include <optional>
#include <chrono>

namespace pnkr::core
{
    template<typename T>
    class LockFreeQueue
    {
    public:

        LockFreeQueue() = default;
        ~LockFreeQueue() = default;

        LockFreeQueue(const LockFreeQueue&) = delete;
        LockFreeQueue& operator=(const LockFreeQueue&) = delete;
        LockFreeQueue(LockFreeQueue&&) = delete;
        LockFreeQueue& operator=(LockFreeQueue&&) = delete;

        void enqueue(T&& item)
        {
            m_queue.enqueue(std::move(item));
        }

        std::optional<T> try_dequeue()
        {
            T item;
            if (m_queue.try_dequeue(item))
            {
                return std::move(item);
            }
            return std::nullopt;
        }

        T wait_dequeue()
        {
            T item;
            m_queue.wait_dequeue(item);
            return item;
        }

        template<typename Rep, typename Period>
        std::optional<T> wait_dequeue_timed(const std::chrono::duration<Rep, Period>& timeout)
        {
            T item;
            if (m_queue.wait_dequeue_timed(item, timeout))
            {
                return std::move(item);
            }
            return std::nullopt;
        }

        bool empty() const
        {
            return m_queue.size_approx() == 0;
        }

        size_t size_approx() const
        {
            return m_queue.size_approx();
        }

    private:
        moodycamel::BlockingConcurrentQueue<T> m_queue;
    };
}
