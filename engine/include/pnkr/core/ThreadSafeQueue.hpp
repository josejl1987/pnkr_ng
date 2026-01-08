#pragma once

#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace pnkr::core
{

    template<typename T>
    class ThreadSafeQueue
    {
    public:
        ThreadSafeQueue() = default;
        ~ThreadSafeQueue() = default;

        ThreadSafeQueue(const ThreadSafeQueue&) = delete;
        ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
        ThreadSafeQueue(ThreadSafeQueue&&) = delete;
        ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

        void enqueue(T&& item)
        {
            {
                std::lock_guard<std::mutex> queueLock(m_mutex);
                m_queue.push_back(std::move(item));
            }
            m_cv.notify_one();
        }

        void enqueue_front(T&& item)
        {
            {
                std::lock_guard<std::mutex> queueLock(m_mutex);

                m_queue.push_front(std::move(item));
            }
            m_cv.notify_one();
        }

        std::optional<T> try_dequeue()
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            if (m_queue.empty())
            {
                return std::nullopt;
            }
            T item = std::move(m_queue.front());
            m_queue.pop_front();
            return item;
        }

        template<typename Rep, typename Period>
        std::optional<T> try_dequeue_for(const std::chrono::duration<Rep, Period>& timeout)
        {
            std::unique_lock<std::mutex> queueLock(m_mutex);
            if (!m_cv.wait_for(queueLock, timeout, [this] { return !m_queue.empty(); }))
            {
                return std::nullopt;
            }
            T item = std::move(m_queue.front());
            m_queue.pop_front();
            return item;
        }

        T dequeue()
        {
            std::unique_lock<std::mutex> queueLock(m_mutex);
            m_cv.wait(queueLock, [this] { return !m_queue.empty(); });
            T item = std::move(m_queue.front());
            m_queue.pop_front();
            return item;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            return m_queue.empty();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            return m_queue.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            m_queue.clear();
        }

    private:
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<T> m_queue;
    };

    template<typename T>
    class PriorityThreadSafeQueue
    {
    public:
        struct PriorityItem
        {
            T item;
            int priority;

            bool operator<(const PriorityItem& other) const
            {
                return priority < other.priority;
            }
        };

        PriorityThreadSafeQueue() = default;
        ~PriorityThreadSafeQueue() = default;

        PriorityThreadSafeQueue(const PriorityThreadSafeQueue&) = delete;
        PriorityThreadSafeQueue& operator=(const PriorityThreadSafeQueue&) = delete;
        PriorityThreadSafeQueue(PriorityThreadSafeQueue&&) = delete;
        PriorityThreadSafeQueue& operator=(PriorityThreadSafeQueue&&) = delete;

        void enqueue(T&& item, int priority)
        {
            {
                std::lock_guard<std::mutex> queueLock(m_mutex);
                m_queue.push({std::move(item), priority});
            }
            m_cv.notify_one();
        }

        std::optional<T> try_dequeue()
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            if (m_queue.empty())
            {
                return std::nullopt;
            }
            T item = std::move(m_queue.top().item);
            m_queue.pop();
            return item;
        }

        template<typename Rep, typename Period>
        std::optional<T> try_dequeue_for(const std::chrono::duration<Rep, Period>& timeout)
        {
            std::unique_lock<std::mutex> queueLock(m_mutex);
            if (!m_cv.wait_for(queueLock, timeout, [this] { return !m_queue.empty(); }))
            {
                return std::nullopt;
            }
            T item = std::move(m_queue.top().item);
            m_queue.pop();
            return item;
        }

        T dequeue()
        {
            std::unique_lock<std::mutex> queueLock(m_mutex);
            m_cv.wait(queueLock, [this] { return !m_queue.empty(); });
            T item = std::move(m_queue.top().item);
            m_queue.pop();
            return item;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            return m_queue.empty();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            return m_queue.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> queueLock(m_mutex);
            while (!m_queue.empty())
            {
                m_queue.pop();
            }
        }

    private:
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::priority_queue<PriorityItem> m_queue;
    };

}
