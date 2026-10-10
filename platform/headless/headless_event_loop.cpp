#include "platform/headless/headless_event_loop.h"

#include <utility>

#include "platform/headless/headless_platform_window.h"
#include "window/platform_window_factory.h"

namespace albor::headless
{
    HeadlessEventLoop::~HeadlessEventLoop() = default;

    void HeadlessEventLoop::set_pump_handler(Task pump) {
        // the pump is owned by the application, so the loop borrows it for the life of the run (§4.1)
        m_pump = std::move(pump);
    }

    void HeadlessEventLoop::run() {
        // a loop with no pump has nothing to do, and pretending otherwise would spin the test (§4.1)
        if (!m_pump)
            return;
        // the loop is marked running first, so a handler observing it sees the truth (§4.1)
        m_running = true;
        // one iteration runs the ui thread tasks, which is what a display link callback would do (§4.1, AD-18b)
        m_pump();
        // the loop stops at the end of the iteration, because a headless run must not block a test (§4.1)
        m_running = false;
    }

    void HeadlessEventLoop::quit() {
        // there is no thread to signal, so the flag is the whole of quitting (§4.1)
        m_running = false;
    }

    bool HeadlessEventLoop::running() const {
        // the flag is what a test asserts on, because nothing else is observable (§4.1)
        return m_running;
    }

    void HeadlessEventLoop::start_frame_pacing() {
        // pacing is only a flag here, because there is no display link to raise ticks (§4.3, AD-16)
        m_pacing = true;
    }

    void HeadlessEventLoop::stop_frame_pacing() {
        // pacing stops with the flag, which a window teardown requires (§4.3)
        m_pacing = false;
    }

    bool HeadlessEventLoop::pacing() const {
        // the flag is what a test synchronizes on instead of a display link callback (§4.3)
        return m_pacing;
    }

    void install_headless_platform() {
        // the loop factory builds the headless loop, which pumps once per call (§5.3, AD-16)
        set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new HeadlessEventLoop()); });
        // the window factory builds the window that has no native window behind it (§5.3, AD-16)
        set_platform_window_factory([](const WindowOptions& options) -> std::unique_ptr<PlatformWindow> {
            // the factory returns the concrete window as the hal, so no headless type escapes the module (§3.5)
            return HeadlessPlatformWindow::create(options);
        });
    }
} // namespace albor::headless
