#include "application/timer.h"

namespace albor
{
    namespace
    {
        // the real steady clock, which is monotonic like every framework deadline (§4.1)
        std::chrono::steady_clock::time_point steady_now() { return std::chrono::steady_clock::now(); }
    } // namespace

    TimerScheduler::TimerScheduler(Clock clock) :
        m_clock(clock ? std::move(clock) : Clock(&steady_now)) {}

    TimerHandle TimerScheduler::schedule_once(TimerCallback callback, std::chrono::milliseconds delay) {
        // a timer without work would fire into nothing, so it is never registered (§4.2)
        if (!callback)
            return 0;
        // the lock is taken because a worker thread may arm a timer while the ui thread fires timers (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry entry;
        entry.handle = m_next_handle++;
        entry.callback = std::move(callback);
        entry.interval = delay;
        entry.deadline = m_clock() + delay;
        entry.one_shot = true;
        m_entries.push_back(std::move(entry));
        // the identifier the registrant may cancel the timer with
        return m_entries.back().handle;
    }

    TimerHandle TimerScheduler::schedule_repeating(TimerCallback callback, std::chrono::milliseconds interval) {
        // a timer without work would fire into nothing, so it is never registered (§4.2)
        if (!callback)
            return 0;
        // the lock is taken because a worker thread may arm a timer while the ui thread fires timers (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry entry;
        entry.handle = m_next_handle++;
        entry.callback = std::move(callback);
        entry.interval = interval;
        entry.deadline = m_clock() + interval;
        entry.one_shot = false;
        m_entries.push_back(std::move(entry));
        // the identifier the registrant may cancel the timer with
        return m_entries.back().handle;
    }

    bool TimerScheduler::scheduled(TimerHandle handle) const {
        // the lock is taken because an arming thread may append while the ui thread inspects (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        // a cancelled timer stays in the table but is no longer registered (§4.2)
        const Entry* entry = find_locked(handle);
        return entry != nullptr && !entry->cancelled;
    }

    bool TimerScheduler::cancel(TimerHandle handle) {
        // the lock is taken because an arming thread may append while the ui thread cancels (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        Entry* entry = find_locked(handle);
        // a stale or already cancelled handle has nothing left to cancel (§4.2)
        if (entry == nullptr || entry->cancelled)
            return false;
        // cancelling marks the entry rather than erasing it, so a concurrent fire cannot miss it (§4.2)
        entry->cancelled = true;
        return true;
    }

    std::size_t TimerScheduler::fire_due() {
        // the callbacks run outside the lock, because one of them may arm or cancel a timer re-entrantly (§4.2)
        std::vector<Entry> due;
        {
            // the lock is taken so that a concurrent arm cannot interleave with the deadline scan (§4.1)
            const std::lock_guard<std::mutex> lock(m_mutex);
            // the deadline is read once, so every timer in this pass is judged against the same instant (§4.1)
            const std::chrono::steady_clock::time_point now = m_clock();
            for (Entry& entry : m_entries) {
                // a cancelled timer never fires again, and a deadline in the future is not due yet (§4.2)
                if (entry.cancelled || entry.deadline > now)
                    continue;
                // a repeating timer is counted and re-armed here, before its callback runs (§4.2)
                entry.fires++;
                // a one shot timer is retired by cancelling it, which is what keeps it out of the next pass (§4.2)
                if (entry.one_shot)
                    entry.cancelled = true;
                else {
                    entry.deadline = now + entry.interval;
                }
                due.push_back(entry);
            }
        }
        std::size_t fired = 0;
        // the due callbacks run on the ui thread, which is the only thread a timer may touch (§4.2)
        for (Entry& entry : due) {
            entry.callback();
            fired++;
        }
        return fired;
    }

    std::chrono::milliseconds TimerScheduler::time_until_next() const {
        // the lock is taken because an arming thread may append while the event loop waits (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point now = m_clock();
        bool found = false;
        std::chrono::steady_clock::time_point earliest = now;
        for (const Entry& entry : m_entries) {
            // a cancelled timer no longer constrains the wait (§4.2)
            if (entry.cancelled)
                continue;
            // the first live deadline seeds the search, and later ones only lower it (§4.1)
            if (!found || entry.deadline < earliest) {
                earliest = entry.deadline;
                found = true;
            }
        }
        // a scheduler whose every timer was cancelled imposes no deadline on the event loop (§4.2)
        if (!found)
            return std::chrono::milliseconds::max();
        // a deadline that already passed yields an immediate wait rather than a negative delay (§4.1)
        if (earliest <= now)
            return std::chrono::milliseconds(0);
        return std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now);
    }

    std::size_t TimerScheduler::size() const {
        // the lock is taken because an arming thread may append while the ui thread counts (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        std::size_t registered = 0;
        // a cancelled timer stays in the table but is no longer registered (§4.2)
        for (const Entry& entry : m_entries) {
            if (!entry.cancelled)
                registered++;
        }
        return registered;
    }

    std::uint64_t TimerScheduler::fire_count(TimerHandle handle) const {
        // the lock is taken because an arming thread may append while the ui thread reads (§4.1)
        const std::lock_guard<std::mutex> lock(m_mutex);
        // a handle that names no timer has never fired (§4.2)
        const Entry* entry = find_locked(handle);
        if (entry == nullptr)
            return 0;
        return entry->fires;
    }

    TimerScheduler::Entry* TimerScheduler::find_locked(TimerHandle handle) {
        for (Entry& entry : m_entries) {
            if (entry.handle == handle)
                return &entry;
        }
        return nullptr;
    }

    const TimerScheduler::Entry* TimerScheduler::find_locked(TimerHandle handle) const {
        for (const Entry& entry : m_entries) {
            if (entry.handle == handle)
                return &entry;
        }
        return nullptr;
    }
} // namespace albor
