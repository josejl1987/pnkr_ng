#pragma once
#include <boost/context/detail/fcontext.hpp>
#include <cstdint>
#include <functional>
#include <cassert>

namespace pnkr::core {

#ifdef Yield
#undef Yield
#endif

    struct Job {
        void (*function)(void*);
        void* arg;
        struct AtomicCounter* counter = nullptr;
    };

    class Fiber {
    public:
        static constexpr size_t STACK_SIZE = 64 * 1024;

        using EntryPoint = std::function<void(Fiber*)>;

        Fiber() = default;

        explicit Fiber(EntryPoint entry, size_t stackSize = STACK_SIZE)
            : m_stackSize(stackSize), m_entry(std::move(entry)) {
            m_stack = new uint8_t[m_stackSize];

            m_context = boost::context::detail::make_fcontext(
                m_stack + m_stackSize,
                m_stackSize,
                &Fiber::FiberProxy
            );
        }

        ~Fiber() {
            if (m_stack) delete[] m_stack;
        }

        void Resume() {
            boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(m_context, this);

            m_context = t.fctx;
        }

        void Yield() {
            if (m_returnContext) {
                boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(m_returnContext, nullptr);

                m_returnContext = t.fctx;
            }
        }
        void SetJob(const Job& job) { m_currentJob = job; }
        const Job& GetJob() const { return m_currentJob; }
        bool IsValid() const { return m_context != nullptr; }

    private:
        static void FiberProxy(boost::context::detail::transfer_t t) {
            Fiber* self = static_cast<Fiber*>(t.data);
            self->m_returnContext = t.fctx;

            if (self->m_entry) {
                self->m_entry(self);
            }

            boost::context::detail::jump_fcontext(self->m_returnContext, nullptr);
        }

        uint8_t* m_stack = nullptr;
        size_t m_stackSize = 0;
        boost::context::detail::fcontext_t m_context = nullptr;
        boost::context::detail::fcontext_t m_returnContext = nullptr;
        EntryPoint m_entry;
        Job m_currentJob{};
    };

}
