#include "albor/platform/linux/wayland/event_loop.h"

#include <cerrno>
#include <cstdint>
#include <utility>

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "albor/platform/linux/wayland/platform_window.h"
#include "albor/platform/linux/wayland/wayland_connection.linux.h"
#include "albor/window/platform_window_factory.h"

namespace albor::wayland
{
    // the opaque wayland state, defined here so that no wayland type reaches the header (§3.5, AD-05)
    struct WaylandEventLoop::State
    {
        // the shared connection the loop polls and dispatches, which the factory built (§5.3)
        std::shared_ptr<WaylandConnection> connection;

        // the timer the frame ticks arrive on, which the pacing arms (§4.3)
        int pacing_fd = -1;

        // whether the pacing timer is armed, which the poll set reads (§4.3)
        bool pacing_armed = false;
    };

    WaylandEventLoop::WaylandEventLoop() :
        m_state(std::make_unique<State>()) {
        // the shared connection is what the loop polls, built or adopted under the default name (§5.3)
        m_state->connection = WaylandConnection::connect(std::string_view());
        // the pacing timer is created up front, so starting the pacing source cannot fail later (§4.3)
        m_state->pacing_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    }

    WaylandEventLoop::~WaylandEventLoop() {
        // the pacing source is stopped before anything else, so no further frame can arrive (§4.3)
        stop_frame_pacing();
        // the timer is closed, because the loop owns it for exactly its own lifetime (§4.3)
        if (m_state->pacing_fd >= 0) {
            close(m_state->pacing_fd);
            m_state->pacing_fd = -1;
        }
        // the connection is dropped with the state, which outlives every window that borrowed it (§4.1)
        m_state->connection = nullptr;
    }

    void WaylandEventLoop::set_pump_handler(Task pump) {
        // the handler is what every pacing tick invokes on the ui thread (§4.1)
        m_pump = std::move(pump);
    }

    void WaylandEventLoop::run() {
        m_running = true;
        // a loop with no connection has nothing to poll, and pretending otherwise would spin (§4.1)
        if (m_state->connection == nullptr) {
            // the run ends here, because there is no compositor to wait on (§4.1)
            m_running = false;
            return;
        }
        // the loop runs until quit is asked from any thread (§4.1)
        while (m_running.load()) {
            // every event that already arrived is dispatched before the wait, so no queue builds up (§4.1)
            m_state->connection->dispatch_pending();
            // the requests the client queued are sent, which the compositor answers (§4.1)
            m_state->connection->flush();
            // the poll set waits on the display first, which is where the compositor events arrive (§4.1)
            pollfd fds[3] = {};
            std::size_t count = 1;
            fds[0].fd = m_state->connection->fd();
            fds[0].events = POLLIN;
#ifdef ALBOR_WAYLAND_LIBDECOR
            // the decoration queue is polled on its own descriptor where it differs from the display (§5.3)
            std::size_t decoration_index = 0;
            const int decoration_fd = m_state->connection->decoration_fd();
            if (decoration_fd >= 0 && decoration_fd != fds[0].fd) {
                fds[count].fd = decoration_fd;
                fds[count].events = POLLIN;
                decoration_index = count;
                count++;
            }
#endif
            // the pacing timer joins the poll set only while it is armed (§4.3)
            std::size_t pacing_index = 0;
            if (m_state->pacing_armed && m_state->pacing_fd >= 0) {
                fds[count].fd = m_state->pacing_fd;
                fds[count].events = POLLIN;
                pacing_index = count;
                count++;
            }
            // the wait is bounded to one millisecond, so a posted task is answered without a native event (§4.1)
            const int ready = poll(fds, static_cast<nfds_t>(count), 1);
            // a poll failure of the permanent kind means the compositor is gone, which ends the run (§4.1)
            if (ready < 0 && errno != EINTR) {
                // the run ends on the failure, which running reports (§4.1)
                break;
            }
            // the display is readable exactly when events arrived, which are read and dispatched now (§4.1)
            if ((fds[0].revents & POLLIN) != 0)
                m_state->connection->dispatch();
#ifdef ALBOR_WAYLAND_LIBDECOR
            // the decoration queue is dispatched when its own descriptor is readable (§5.3)
            if (decoration_index != 0 && (fds[decoration_index].revents & POLLIN) != 0)
                m_state->connection->dispatch_decorations();
#endif
            // the pacing timer expired when its descriptor is readable, which counts and pumps (§4.3)
            if (pacing_index != 0 && (fds[pacing_index].revents & POLLIN) != 0) {
                // the expiration count is read once, because a stalled loop coalesces its missed ticks (§4.3)
                std::uint64_t expirations = 0;
                const std::ptrdiff_t read_bytes = static_cast<std::ptrdiff_t>(read(m_state->pacing_fd, &expirations, sizeof(expirations)));
                // a count that was read is added, which the example reports the cadence through (§4.3)
                if (read_bytes == static_cast<std::ptrdiff_t>(sizeof(expirations)))
                    m_frame_ticks.fetch_add(expirations);
                // the pump runs once for the batch, which is what the tick was for (§4.3)
                if (m_pump)
                    m_pump();
            }
        }
        // the run is over, which running reports (§4.1)
        m_running = false;
    }

    void WaylandEventLoop::quit() {
        // the flag is atomic, so a worker thread may terminate the application (§4.1)
        m_running = false;
    }

    bool WaylandEventLoop::running() const {
        // the flag is what a caller asserts on, because nothing else is observable (§4.1)
        return m_running.load();
    }

    void WaylandEventLoop::start_frame_pacing() {
        // a timer that could not be created leaves the loop running without pacing rather than crashing (§4.3)
        if (m_state->pacing_fd < 0)
            return;
        // the period is one refresh in nanoseconds, which is the unit a timerfd counts in (§4.3)
        const long period_ns = static_cast<long>(1000000000.0 / refresh_hz());
        // the timer arms one period out and repeats at it, which paces the frames (§4.3)
        itimerspec schedule{};
        schedule.it_value.tv_nsec = period_ns;
        schedule.it_interval.tv_nsec = period_ns;
        // a timer that will not arm leaves the loop unpaced, which the armed flag records (§4.3)
        if (timerfd_settime(m_state->pacing_fd, 0, &schedule, nullptr) == 0)
            m_state->pacing_armed = true;
    }

    void WaylandEventLoop::stop_frame_pacing() {
        // a timer that was never created has nothing to disarm (§4.3)
        if (m_state->pacing_fd < 0)
            return;
        // the timer is disarmed, which a later start arms again from zero (§4.3)
        itimerspec disarmed{};
        timerfd_settime(m_state->pacing_fd, 0, &disarmed, nullptr);
        m_state->pacing_armed = false;
    }

    std::uint64_t WaylandEventLoop::frame_ticks() const {
        // the counter is atomic, because the loop thread increments it (§4.3)
        return m_frame_ticks.load();
    }

    double WaylandEventLoop::refresh_hz() const {
        // the rate is what the outputs reported when the connection was built (§4.3)
        return m_state->connection != nullptr ? m_state->connection->refresh_hz() : 60.0;
    }

    void WaylandEventLoop::deliver_on_ui_thread() {
        // the loop may have been torn down while the tick was due (§4.2)
        if (!m_running.load())
            return;
        // the tick is counted so that the example can report the cadence it reached (§4.3)
        m_frame_ticks.fetch_add(1);
        // the pump runs on the ui thread, which is where posted tasks and timers belong (§4.1)
        if (!m_pump)
            return;
        m_pump();
    }

    void install_wayland_platform() {
        // the loop factory builds the wayland backed loop, which an application runs on (§5.3, AD-16)
        set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new WaylandEventLoop()); });
        // the window factory builds the xdg window that carries the surface (§5.3, AD-16)
        set_platform_window_factory([](const WindowOptions& options) -> std::unique_ptr<PlatformWindow> {
            // the factory returns the concrete window as the hal, so no wayland type escapes the module (§3.5)
            return WaylandPlatformWindow::create(options);
        });
    }
} // namespace albor::wayland
