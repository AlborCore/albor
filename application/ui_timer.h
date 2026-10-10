#pragma once

#include <chrono>

#include "application/task_queue.h"
#include "application/timer.h"

namespace albor
{
    // the RAII timer bound to the UI thread, whose destruction cancels it immediately (§4.2)
    class Timer
    {
    public:
        // build a one shot timer that fires once after the delay
        Timer(TimerScheduler& scheduler, TimerCallback callback, std::chrono::milliseconds delay);

        // build a repeating timer that fires at every interval until it is destroyed (§4.2)
        static Timer repeating(TimerScheduler& scheduler, TimerCallback callback, std::chrono::milliseconds interval);

        // cancel the timer before it fires again (§4.2)
        ~Timer();

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;

        // take over the scheduling of another timer, which gives up its own handle
        Timer(Timer&& other) noexcept;

        // take over the scheduling of another timer, cancelling the one this object held
        Timer& operator=(Timer&& other) noexcept;

        // report whether the timer is still registered
        [[nodiscard]] bool active() const;

        // hand out the identifier the timer was registered under
        [[nodiscard]] TimerHandle handle() const;

        // hand out how many times the timer already fired
        [[nodiscard]] std::uint64_t fire_count() const;

        // cancel the timer explicitly, which its destruction does as well (§4.2)
        void cancel();

    private:
        // adopt a registration that another object produced
        Timer(TimerScheduler& scheduler, TimerHandle handle);

        // the scheduler that owns the registration
        TimerScheduler* m_scheduler = nullptr;

        // the identifier of the registration, where zero names no timer
        TimerHandle m_handle = 0;
    };
} // namespace albor
