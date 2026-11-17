#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "albor/application/application.h"

namespace albor::wayland
{
    // the wayland backed native loop that paces frames through a timer (§4.3, AD-16, AD-22)
    class WaylandEventLoop final : public PlatformEventLoop
    {
    public:
        // build the loop over the shared wayland connection, which a compositor that is unreachable leaves pacing less
        WaylandEventLoop();

        // disarm the pacing timer and release the connection
        ~WaylandEventLoop() override;

        WaylandEventLoop(const WaylandEventLoop&) = delete;
        WaylandEventLoop& operator=(const WaylandEventLoop&) = delete;
        WaylandEventLoop(WaylandEventLoop&&) = delete;
        WaylandEventLoop& operator=(WaylandEventLoop&&) = delete;

        // override: install the callback the loop invokes once per pacing tick
        void set_pump_handler(Task pump) override;

        // override: run the native loop until quit is called
        void run() override;

        // override: ask the native loop to stop, which is thread safe
        void quit() override;

        // override: report whether the native loop is running
        [[nodiscard]] bool running() const override;

        // override: arm the timer that raises frame ticks
        void start_frame_pacing() override;

        // override: disarm the timer
        void stop_frame_pacing() override;

        // hand out how many frames the timer has raised, which the example reports
        [[nodiscard]] std::uint64_t frame_ticks() const;

        // hand out the refresh rate the loop paces at, which the outputs report (§4.3)
        [[nodiscard]] double refresh_hz() const;

        // count one pacing tick and run the pump, which the loop runs on the ui thread (§4.1, §4.3)
        void deliver_on_ui_thread();

    private:
        // the opaque wayland state, so that no platform type reaches this header (§3.5)
        struct State;

        // the opaque wayland state, which holds the connection and the pacing timer (§4.3)
        std::unique_ptr<State> m_state;

        // the callback the loop invokes once per pacing tick, on the ui thread (§4.1)
        Task m_pump;

        // whether the loop is running, which quit clears from any thread (§4.1)
        std::atomic<bool> m_running{false};

        // how many frames the timer raised
        std::atomic<std::uint64_t> m_frame_ticks{0};
    };

    // install the wayland platform loop and window factory, which an application calls before it runs (§5.3)
    void install_wayland_platform();
} // namespace albor::wayland
