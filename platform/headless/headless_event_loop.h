#pragma once

#include <memory>

#include "application/application.h"

namespace albor::headless
{
    // the event loop that pumps only when a test asks, because there is no display link to raise ticks (§4.3, AD-16)
    class HeadlessEventLoop final : public PlatformEventLoop
    {
    public:
        HeadlessEventLoop() = default;
        ~HeadlessEventLoop() override;

        HeadlessEventLoop(const HeadlessEventLoop&) = delete;
        HeadlessEventLoop& operator=(const HeadlessEventLoop&) = delete;
        HeadlessEventLoop(HeadlessEventLoop&&) = delete;
        HeadlessEventLoop& operator=(HeadlessEventLoop&&) = delete;

        // override: install the callback the loop invokes per iteration, which is how the ui thread is pumped (§4.1)
        void set_pump_handler(Task pump) override;

        // override: run one iteration and return, because a headless loop must not block a test (§4.1)
        void run() override;

        // override: ask the loop to stop, which ends a headless run at the next iteration (§4.1)
        void quit() override;

        // override: report whether the loop is running (§4.1)
        [[nodiscard]] bool running() const override;

        // override: record that pacing was asked for, which a test asserts on instead of a display link (§4.3)
        void start_frame_pacing() override;

        // override: record that pacing was stopped, which a teardown requires (§4.3)
        void stop_frame_pacing() override;

        // report whether pacing is running, which is what a headless test synchronizes on (§4.3)
        [[nodiscard]] bool pacing() const;

    private:
        // the callback the loop invokes per iteration, which the application owns (§4.1)
        Task m_pump;

        // whether the loop is running, which run sets and quit clears (§4.1)
        bool m_running = false;

        // whether pacing was asked for, which replaces the display link a real platform drives (§4.3, AD-16)
        bool m_pacing = false;
    };

    // install the headless window and loop factories, which a ci run does instead of a platform backend (§5.3, AD-16)
    void install_headless_platform();
} // namespace albor::headless
