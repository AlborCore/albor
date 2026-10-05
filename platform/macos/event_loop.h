#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "application/application.h"

namespace albor::macos
{
    // the appkit backed native loop that drives frames through a core video display link (§4.3, AD-16)
    class MacosEventLoop final : public PlatformEventLoop
    {
    public:
        // build the loop without starting it, which the application does when it runs
        MacosEventLoop();

        // stop the display link and release the appkit objects the loop owns
        ~MacosEventLoop() override;

        MacosEventLoop(const MacosEventLoop&) = delete;
        MacosEventLoop& operator=(const MacosEventLoop&) = delete;
        MacosEventLoop(MacosEventLoop&&) = delete;
        MacosEventLoop& operator=(MacosEventLoop&&) = delete;

        // override: install the callback the loop invokes once per iteration
        void set_pump_handler(Task pump) override;

        // override: run the native loop until quit is called
        void run() override;

        // override: ask the native loop to stop, which is thread safe
        void quit() override;

        // override: report whether the native loop is running
        [[nodiscard]] bool running() const override;

        // override: start the display link that raises frame ticks
        void start_frame_pacing() override;

        // override: stop the display link
        void stop_frame_pacing() override;

        // hand out how many frames the display link has raised, which the example reports
        [[nodiscard]] std::uint64_t frame_ticks() const;

        // hand out the display refresh rate the loop paces at, which the screen reports (§4.3)
        [[nodiscard]] double refresh_hz() const;

        // count one pacing tick and run the pump, which the display link marshals onto the ui thread (§4.1, §4.3)
        void deliver_on_ui_thread();

    private:
        // the opaque appkit and core video state, so that no platform type reaches this header (§3.5)
        struct State;

        // the opaque appkit and core video state, so that no platform type reaches this header (§3.5)
        std::unique_ptr<State> m_state;

        // the callback the loop invokes once per iteration, on the ui thread (§4.1)
        Task m_pump;

        // whether the loop is running, which quit clears from any thread (§4.1)
        std::atomic<bool> m_running{false};

        // how many frames the display link raised
        std::atomic<std::uint64_t> m_frame_ticks{0};
    };

    // install the macOS platform loop and window factory, which an application calls before it runs (§5.3)
    void install_macos_platform();
} // namespace albor::macos
