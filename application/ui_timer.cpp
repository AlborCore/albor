#include "application/ui_timer.h"

namespace albor
{
    Timer::Timer(TimerScheduler& scheduler, TimerCallback callback, std::chrono::milliseconds delay) :
        m_scheduler(&scheduler) {
        // a timer without work never fires, so it registers nothing (§4.2)
        if (!callback) {
            m_scheduler = nullptr;
            return;
        }

        m_handle = scheduler.schedule_once(std::move(callback), delay);
    }

    Timer Timer::repeating(TimerScheduler& scheduler, TimerCallback callback, std::chrono::milliseconds interval) {
        // a timer without work never fires, so it registers nothing (§4.2)
        if (!callback)
            return Timer(scheduler, TimerCallback{}, interval);
        // the registration is adopted through the private constructor, which does not arm a one shot (§4.2)
        return Timer(scheduler, scheduler.schedule_repeating(std::move(callback), interval));
    }

    Timer::~Timer() {
        // destruction is the cancellation, so no future firing survives the timer (§4.2)
        cancel();
    }

    Timer::Timer(Timer&& other) noexcept :
        m_scheduler(other.m_scheduler), m_handle(other.m_handle) {
        // the moved from object gives up its registration, so its destruction cancels nothing (§4.2)
        other.m_scheduler = nullptr;
        other.m_handle = 0;
    }

    Timer& Timer::operator=(Timer&& other) noexcept {
        // self assignment would cancel the very timer it is meant to keep, so it is rejected (§4.2)
        if (this == &other)
            return *this;
        // the timer this object held is cancelled before the new one is adopted (§4.2)
        cancel();
        m_scheduler = other.m_scheduler;
        m_handle = other.m_handle;
        // the moved from object gives up its registration (§4.2)
        other.m_scheduler = nullptr;
        other.m_handle = 0;
        return *this;
    }

    bool Timer::active() const {
        // a timer that registered nothing is never active (§4.2)
        if (m_scheduler == nullptr)
            return false;
        return m_scheduler->scheduled(m_handle);
    }

    TimerHandle Timer::handle() const {
        // the identifier is what the scheduler knows the registration by (§4.2)
        return m_handle;
    }

    std::uint64_t Timer::fire_count() const {
        // a timer that registered nothing never fired (§4.2)
        if (m_scheduler == nullptr)
            return 0;
        return m_scheduler->fire_count(m_handle);
    }

    void Timer::cancel() {
        // a timer that registered nothing has nothing to cancel (§4.2)
        if (m_scheduler == nullptr)
            return;
        m_scheduler->cancel(m_handle);
        // the registration is forgotten, so a later cancellation cannot reach a recycled handle (§4.2)
        m_scheduler = nullptr;
        m_handle = 0;
    }

    Timer::Timer(TimerScheduler& scheduler, TimerHandle handle) :
        m_scheduler(&scheduler), m_handle(handle) {}
} // namespace albor
