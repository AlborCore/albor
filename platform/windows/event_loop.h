#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "application/application.h"

namespace albor::windows
{
    // the win32 backed native loop that drives frames through a timer thread (§4.3, AD-16)
    class Win32EventLoop final : public PlatformEventLoop
    {
    public:
        // build the loop without starting it, which the application does when it runs
        Win32EventLoop();

        // stop the pacing thread and release the kernel objects the loop owns
        ~Win32EventLoop() override;

        Win32EventLoop(const Win32EventLoop&) = delete;
        Win32EventLoop& operator=(const Win32EventLoop&) = delete;
        Win32EventLoop(Win32EventLoop&&) = delete;
        Win32EventLoop& operator=(Win32EventLoop&&) = delete;

        // override: install the callback the loop invokes once per frame tick
        void set_pump_handler(Task pump) override;

        // override: run the native loop until quit is called
        void run() override;

        // override: ask the native loop to stop, which is thread safe
        void quit() override;

        // override: report whether the native loop is running
        [[nodiscard]] bool running() const override;

        // override: start the timer thread that raises frame ticks
        void start_frame_pacing() override;

        // override: stop the timer thread
        void stop_frame_pacing() override;

        // hand out how many frames the pacing thread has raised, which the example reports
        [[nodiscard]] std::uint64_t frame_ticks() const;

        // hand out the display refresh rate the loop paces at, which the screen reports (§4.3)
        [[nodiscard]] double refresh_hz() const;

        // count one pacing tick and run the pump, which the pacing thread marshals onto the ui thread (§4.1, §4.3)
        void deliver_on_ui_thread();

    private:
        // the opaque win32 state, so that no platform type reaches this header (§3.5)
        struct State;

        // the opaque win32 state, which holds the timer, the stop event and the pacing thread (§4.3)
        std::unique_ptr<State> m_state;

        // the callback the loop invokes once per frame tick, on the ui thread (§4.1)
        Task m_pump;

        // whether the loop is running, which quit clears from any thread (§4.1)
        std::atomic<bool> m_running{false};

        // how many frames the pacing thread raised
        std::atomic<std::uint64_t> m_frame_ticks{0};
    };

    // install the windows platform loop and window factory, which an application calls before it runs (§5.3)
    void install_windows_platform();
} // namespace albor::windows
