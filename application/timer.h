#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "application/task_queue.h"

namespace albor
{
    // the callback a timer fires on the UI thread (§4.2)
    using TimerCallback = std::function<void()>;

    // the clock a scheduler reads, which a test replaces with a deterministic one (§4.2)
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    // the registry that owns every UI thread timer and fires the ones that are due (§4.2)
    class TimerScheduler
    {
    public:
        // build a scheduler over the real steady clock
        explicit TimerScheduler(Clock clock = Clock{});

        TimerScheduler(const TimerScheduler&) = delete;
        TimerScheduler& operator=(const TimerScheduler&) = delete;
        TimerScheduler(TimerScheduler&&) = delete;
        TimerScheduler& operator=(TimerScheduler&&) = delete;
        ~TimerScheduler() = default;

        // register a one shot timer, which fires once at the deadline
        [[nodiscard]] TimerHandle schedule_once(TimerCallback callback, std::chrono::milliseconds delay);

        // register a repeating timer, which fires at every deadline until it is cancelled (§4.2)
        [[nodiscard]] TimerHandle schedule_repeating(TimerCallback callback, std::chrono::milliseconds interval);

        // report whether a handle still names a registered timer
        [[nodiscard]] bool scheduled(TimerHandle handle) const;

        // cancel a timer before it fires again, thread safe (§4.2)
        bool cancel(TimerHandle handle);

        // fire every timer whose deadline has passed and hand out the number of callbacks that ran (§4.2)
        std::size_t fire_due();

        // hand out the delay until the next deadline, which is what the event loop waits on (§4.1)
        [[nodiscard]] std::chrono::milliseconds time_until_next() const;

        // hand out the number of registered timers
        [[nodiscard]] std::size_t size() const;

        // report whether a registered timer fired at least once, which a test asserts on
        [[nodiscard]] std::uint64_t fire_count(TimerHandle handle) const;

    private:
        // one registered timer, with the deadline it next fires at
        struct Entry
        {
            // the identifier the registrant received
            TimerHandle handle = 0;

            // the work the timer runs on the UI thread
            TimerCallback callback;

            // the delay between two firings, which is zero for a one shot timer
            std::chrono::milliseconds interval;

            // the deadline the next firing is due at
            std::chrono::steady_clock::time_point deadline;

            // how many times the timer already fired
            std::uint64_t fires = 0;

            // whether the timer stops after its first firing
            bool one_shot = false;

            // whether the timer was cancelled and no longer fires (§4.2)
            bool cancelled = false;
        };

        // find the entry a handle names, which the caller already holds the lock for
        Entry* find_locked(TimerHandle handle);

        // find the entry a handle names for reading, which the caller already holds the lock for
        const Entry* find_locked(TimerHandle handle) const;

        // the registered timers
        std::vector<Entry> m_entries;

        // the identifier the next registered timer receives, where zero is never handed out
        TimerHandle m_next_handle = 1;

        // the clock the deadlines are read from
        Clock m_clock;

        // the lock every scheduler operation holds
        mutable std::mutex m_mutex;
    };
} // namespace albor
