#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

#include "application/lifetime_token.h"

namespace albor
{
    // the unit of work the framework schedules on the UI thread (§4.1)
    using Task = std::function<void()>;

    // the identifier of a posted task, where zero names no task
    using TaskHandle = std::uint64_t;

    // the identifier of a registered timer, where zero names no timer
    using TimerHandle = std::uint64_t;

    // the thread safe queue that carries tasks from any thread to the UI thread (§4.1)
    class TaskQueue
    {
    public:
        TaskQueue() = default;

        TaskQueue(const TaskQueue&) = delete;
        TaskQueue& operator=(const TaskQueue&) = delete;
        TaskQueue(TaskQueue&&) = delete;
        TaskQueue& operator=(TaskQueue&&) = delete;
        ~TaskQueue() = default;

        // post a task to the UI thread and hand out its handle, thread safe (§4.1)
        [[nodiscard]] TaskHandle post(Task task);

        // post a guarded task, which is dropped when its token expired before the task ran (§4.2)
        [[nodiscard]] TaskHandle post_guarded(Task task, const LifetimeToken& guard);

        // report whether a handle still names a queued task
        [[nodiscard]] bool queued(TaskHandle handle) const;

        // cancel a queued task before it runs, thread safe
        bool cancel(TaskHandle handle);

        // run every queued task on the calling thread, which is the UI thread (§4.1)
        std::size_t drain();

        // hand out the number of tasks waiting to run
        [[nodiscard]] std::size_t pending() const;

        // block until a task arrives or the timeout expires, which is what the event loop waits on (§4.1)
        void wait_for_task(std::chrono::milliseconds timeout);

        // report whether the queue was woken from a wait, which lets a caller drain without spinning (§4.1)
        [[nodiscard]] bool woken() const;

        // clear the wake flag, which the event loop does before it pumps
        void acknowledge_wake();

    private:
        // one queued task, with the guard it observes and whether it was cancelled
        struct Entry
        {
            // the identifier the poster received
            TaskHandle handle = 0;

            // the work to run on the UI thread
            Task task;

            // the guard the task observes, which only post_guarded ever sets (§4.2)
            LifetimeToken guard;

            // whether the task carries a guard at all, because an unguarded task must not be
            // mistaken for one whose token expired: a default LifetimeToken is expired, so the
            // presence of a guard cannot be inferred from whether the token still guards (§4.2)
            bool guarded = false;

            // whether the task was cancelled before it ran
            bool cancelled = false;
        };

        // the waiting tasks, in the order they were posted
        std::deque<Entry> m_entries;

        // the identifier the next posted task receives, where zero is never handed out
        TaskHandle m_next_handle = 1;

        // the flag a poster raises to wake the waiting event loop
        bool m_woken = false;

        // the lock every queue operation holds
        mutable std::mutex m_mutex;

        // the condition the event loop waits on until a task arrives (§4.1)
        std::condition_variable m_condition;
    };
} // namespace albor
