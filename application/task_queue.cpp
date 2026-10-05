#include "application/task_queue.h"

namespace albor
{
    TaskHandle TaskQueue::post(Task task) {
        // an unguarded task is queued directly rather than through post_guarded, because a default
        // token is expired and would make the drain drop the task before it ever runs (§4.2)
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry entry;
        entry.handle = m_next_handle++;
        entry.task = std::move(task);
        entry.guarded = false;
        m_entries.push_back(std::move(entry));
        // the waiting event loop is woken so that a posted task runs promptly rather than at the next native event (§4.1)
        m_woken = true;
        m_condition.notify_all();
        // the identifier the poster may cancel the task with
        return m_entries.back().handle;
    }

    TaskHandle TaskQueue::post_guarded(Task task, const LifetimeToken& guard) {
        // the lock is taken before the queue is touched, because any thread may post (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry entry;
        entry.handle = m_next_handle++;
        entry.task = std::move(task);
        entry.guard = guard;
        entry.guarded = true;
        m_entries.push_back(std::move(entry));
        // the waiting event loop is woken so that a posted task runs promptly rather than at the next native event (§4.1)
        m_woken = true;
        m_condition.notify_all();
        // the identifier the poster may cancel the task with
        return m_entries.back().handle;
    }

    bool TaskQueue::queued(TaskHandle handle) const {
        // the lock is taken because a poster may be appending while the ui thread inspects (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        // every queued entry is inspected, because a cancelled task keeps its slot until it is drained (§4.1)
        for (const Entry& entry : m_entries) {
            if (entry.handle == handle)
                return true;
        }
        return false;
    }

    bool TaskQueue::cancel(TaskHandle handle) {
        // the lock is taken because a poster may be appending while the ui thread cancels (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        // a cancelled task is marked rather than erased, because drain holds an index into the queue (§4.1)
        for (Entry& entry : m_entries) {
            if (entry.handle == handle && !entry.cancelled) {
                entry.cancelled = true;
                return true;
            }
        }
        return false;
    }

    std::size_t TaskQueue::drain() {
        std::size_t executed = 0;
        // the queue is emptied under the lock, then the tasks run without it, so a task may post again (§4.1)
        while (true) {
            std::deque<Entry> batch;
            {
                // taking the whole queue under one lock keeps a re-entrant post for the next pass rather than losing it
                const std::lock_guard<std::mutex> lock(m_mutex);
                batch.swap(m_entries);
            }
            // an empty batch means no task posted another one, so the drain is over (§4.1)
            if (batch.empty())
                break;
            for (Entry& entry : batch) {
                // a cancelled task is dropped without running its work (§4.1)
                if (entry.cancelled)
                    continue;
                // only a guarded task is dropped on an expired token, because an unguarded task has no token
                // to expire and would otherwise never run (§4.2)
                if (entry.guarded && entry.guard.expired())
                    continue;
                entry.task();
                executed++;
            }
        }
        return executed;
    }

    std::size_t TaskQueue::pending() const {
        // the lock is taken because a poster may be appending while the ui thread counts (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries.size();
    }

    void TaskQueue::wait_for_task(std::chrono::milliseconds timeout) {
        // the unique lock is what the condition variable waits on (§4.1)
        std::unique_lock<std::mutex> lock(m_mutex);
        // a wake that arrived before the wait started must not be missed, so the flag is the predicate (§4.1)
        m_condition.wait_for(lock, timeout, [this] { return m_woken; });
    }

    bool TaskQueue::woken() const {
        // the lock is taken because a poster may raise the flag while the event loop reads it (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_woken;
    }

    void TaskQueue::acknowledge_wake() {
        // the lock is taken so that a poster cannot raise the flag between the read and the clear (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_woken = false;
    }
} // namespace albor
