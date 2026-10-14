#include "platform/windows/event_loop.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

#include "platform/windows/platform_window.h"
#include "window/platform_window_factory.h"

namespace albor::windows
{
    namespace
    {
        // the posted message one frame tick arrives on, which reaches the loop and no window procedure (§4.3)
        constexpr UINT WM_FRAME_TICK = WM_APP + 1;

        // the high resolution waitable timer flag, valued directly because no version macro guards the sdk header (§4.3)
        constexpr DWORD HIGH_RESOLUTION_TIMER = 0x00000002;

        // the refresh rate the active display reports, which is the pacing target (§4.3)
        double query_refresh_hz() {
            // the active paths and modes are the shapes the display configuration answers with (§4.3)
            UINT32 path_count = 0;
            UINT32 mode_count = 0;
            // the buffer sizes are read first, because the configuration fills what they ask for (§4.3)
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_IN_PATHS, &path_count, &mode_count) != ERROR_SUCCESS)
                return 60.0;
            // the buffers are sized from the counts, because the configuration writes into them (§4.3)
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
            // a display configuration that will not come up leaves the fallback rate, which still paces (§4.3)
            if (QueryDisplayConfig(QDC_ONLY_ACTIVE_IN_PATHS, &path_count, paths.data(), modes.data(), nullptr) != ERROR_SUCCESS)
                return 60.0;
            // the highest rate wins, because a mixed desk paces to the fastest screen a window can sit on (§4.3)
            double best = 60.0;
            for (UINT32 index = 0; index < path_count; index++) {
                // the mode index names the refresh, where an invalid one names nothing (§4.3)
                const DISPLAYCONFIG_PATH_TARGET_INFO& target = paths[index].targetInfo;
                if (target.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || target.modeInfoIdx >= mode_count)
                    continue;
                // the refresh arrives as a rational, which divides into hertz through its denominator (§4.3)
                const DISPLAYCONFIG_RATIONAL rate = modes[target.modeInfoIdx].targetMode.signalInfo.refreshRate;
                // a zero denominator would divide by nothing, which is the quiet skip (§4.3)
                if (rate.Denominator == 0)
                    continue;
                const double hz = static_cast<double>(rate.Numerator) / static_cast<double>(rate.Denominator);
                if (hz > best)
                    best = hz;
            }
            return best;
        }

        // what the pacing thread waits on, which names no platform type the header would carry (§4.3)
        struct PacingContext
        {
            // the timer that signals once per refresh period
            HANDLE timer = nullptr;

            // the event that stops the thread
            HANDLE stop = nullptr;

            // the ui thread the ticks are posted to, which is the loop's own thread (§4.1)
            DWORD ui_thread_id = 0;

            // the loop the ticks are delivered to, which outlives every wait the thread makes (§4.2)
            Win32EventLoop* loop = nullptr;
        };

        // the pacing thread body, which waits for the timer and posts one tick per wait (§4.3)
        DWORD WINAPI pacing_body(void* parameter) {
            // the context is the loop's own, which the stop and the join bracket the lifetime of (§4.2)
            PacingContext* context = static_cast<PacingContext*>(parameter);
            // the stop is waited for first, so signalling it ends the thread even while a tick is due (§4.3)
            HANDLE waits[2] = {context->stop, context->timer};
            for (;;) {
                const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                // the stop event or a failure ends the thread, because nothing is left to pace (§4.3)
                if (wait != WAIT_OBJECT_0 + 1)
                    return 0;
                // the tick is posted to the ui thread, because the pump draws and must not run here (§4.3, AD-21)
                PostThreadMessageW(context->ui_thread_id, WM_FRAME_TICK, 0, 0);
            }
        }
    } // namespace

    // the opaque win32 state, defined here so that no platform type reaches the header (§3.5, AD-05)
    struct Win32EventLoop::State
    {
        // the ui thread the loop runs on, which every posted tick addresses (§4.1)
        DWORD ui_thread_id = 0;

        // the timer that signals once per refresh period, which is the pacing source (§4.3)
        HANDLE tick_timer = nullptr;

        // the manual reset event that stops the pacing thread, which a teardown signals (§4.3)
        HANDLE stop_event = nullptr;

        // the pacing thread, which posts one frame tick per refresh to the ui thread (§4.3)
        HANDLE pacing_thread = nullptr;

        // what the pacing thread waits on, which the thread body reads until the join returns (§4.2)
        PacingContext pacing;

        // the refresh rate the display reports, which is the pacing target (§4.3)
        double refresh_hz = 60.0;
    };

    Win32EventLoop::Win32EventLoop() :
        m_state(std::make_unique<State>()) {
        // the ui thread is the thread the application runs on, which the window procedures also run on (§4.1)
        m_state->ui_thread_id = GetCurrentThreadId();
        // the refresh rate is read from the display, because that is the cadence a frame should match (§4.3)
        m_state->refresh_hz = query_refresh_hz();
        // the stop event is manual reset, so one signal wakes the pacing thread for the last time (§4.3)
        m_state->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        // the high resolution timer is the accurate one, because a frame period is far below the default clock (§4.3)
        m_state->tick_timer = CreateWaitableTimerExW(nullptr, nullptr, HIGH_RESOLUTION_TIMER, TIMER_MODIFY_STATE | SYNCHRONIZE);
        // an os that predates the high resolution timer falls back to the plain one, which still paces (§4.3)
        if (m_state->tick_timer == nullptr)
            m_state->tick_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }

    Win32EventLoop::~Win32EventLoop() {
        // the pacing thread is stopped before anything else, so no further frame can arrive (§4.3)
        stop_frame_pacing();
        // the kernel objects are closed, because the loop owns them for exactly its own lifetime (§4.3)
        if (m_state->tick_timer != nullptr) {
            CloseHandle(m_state->tick_timer);
            m_state->tick_timer = nullptr;
        }
        if (m_state->stop_event != nullptr) {
            CloseHandle(m_state->stop_event);
            m_state->stop_event = nullptr;
        }
    }

    void Win32EventLoop::set_pump_handler(Task pump) {
        // the handler is what a posted tick invokes on the ui thread (§4.1)
        m_pump = std::move(pump);
    }

    void Win32EventLoop::run() {
        m_running = true;
        // the message loop runs until quit posts its exit, which is the win32 way to leave a run (§4.1)
        while (m_running) {
            // every queued message is pulled, because win32 delivers the ordinary native path through them (§4.1)
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                // the exit message ends the loop, which is what quit posted from any thread (§4.1)
                if (message.message == WM_QUIT) {
                    m_running = false;
                    break;
                }
                // a posted tick is pumped here, because a thread message never reaches a window procedure (§4.3)
                if (message.message == WM_FRAME_TICK) {
                    deliver_on_ui_thread();
                    continue;
                }
                // the ordinary native message is translated and dispatched, which is where a window procedure runs (§4.1)
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            // the loop sleeps until input or a posted message arrives, bounded to one frame of latency (§4.1)
            if (m_running)
                MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    void Win32EventLoop::quit() {
        // the flag is atomic, so a worker thread may terminate the application (§4.1)
        m_running = false;
        // the pacing source is stopped as well, because the loop is gone (§4.3)
        stop_frame_pacing();
        // the exit is posted to the ui thread, which is what unblocks a run that is waiting (§4.1)
        PostThreadMessageW(m_state->ui_thread_id, WM_QUIT, 0, 0);
    }

    bool Win32EventLoop::running() const {
        // the pacing thread consults this through the delivery, so it must be atomic (§4.3)
        return m_running.load();
    }

    void Win32EventLoop::start_frame_pacing() {
        // a pacing thread that is already running is not started again, because the timer is one per loop (§4.3)
        if (m_state->pacing_thread != nullptr || m_state->tick_timer == nullptr || m_state->stop_event == nullptr)
            return;
        // the period is one refresh in 100 nanosecond units, which is the unit a waitable timer counts in (§4.3)
        const LONGLONG period_100ns = static_cast<LONGLONG>(10000000.0 / m_state->refresh_hz);
        // the due time is one period ahead and the period repeats it, so the timer free runs at the refresh (§4.3)
        LARGE_INTEGER due;
        due.QuadPart = -period_100ns;
        // a timer that will not arm leaves the loop running without pacing, rather than crashing (§4.3)
        if (!SetWaitableTimer(m_state->tick_timer, &due, static_cast<LONG>(period_100ns / 10000), nullptr, nullptr, FALSE))
            return;
        // the context is handed to the thread, which is what names the timer, the stop and the ui thread (§4.3)
        m_state->pacing.timer = m_state->tick_timer;
        m_state->pacing.stop = m_state->stop_event;
        m_state->pacing.ui_thread_id = m_state->ui_thread_id;
        m_state->pacing.loop = this;
        // the thread is created here, because only this function starts pacing (§4.3)
        m_state->pacing_thread = CreateThread(nullptr, 0, &pacing_body, &m_state->pacing, 0, nullptr);
    }

    void Win32EventLoop::stop_frame_pacing() {
        // a pacing thread that was never started has nothing to stop (§4.3)
        if (m_state->pacing_thread == nullptr)
            return;
        // the stop event wakes the pacing thread, which leaves its wait before the join below returns (§4.3)
        SetEvent(m_state->stop_event);
        // the join blocks until the thread returned, so no tick can arrive afterwards (§4.3)
        WaitForSingleObject(m_state->pacing_thread, INFINITE);
        // the thread object is closed, because a joined thread is exactly one the loop owned (§4.3)
        CloseHandle(m_state->pacing_thread);
        m_state->pacing_thread = nullptr;
        // the timer is cancelled, because a later pacing run arms it again from zero (§4.3)
        CancelWaitableTimer(m_state->tick_timer);
        // the stop event is reset, because a later pacing run waits on it again (§4.3)
        ResetEvent(m_state->stop_event);
    }

    std::uint64_t Win32EventLoop::frame_ticks() const {
        // the counter is atomic, because the pacing thread increments it (§4.3)
        return m_frame_ticks.load();
    }

    double Win32EventLoop::refresh_hz() const {
        // the rate is what the display reported when the loop was built (§4.3)
        return m_state->refresh_hz;
    }

    void Win32EventLoop::deliver_on_ui_thread() {
        // the loop may have been torn down while the tick waited in the message queue (§4.2)
        if (!m_running.load())
            return;
        // the tick is counted so that the example can report the cadence it reached (§4.3)
        m_frame_ticks.fetch_add(1);
        // the pump runs on the ui thread, which is where posted tasks and timers belong (§4.1)
        if (!m_pump)
            return;
        m_pump();
    }

    void install_windows_platform() {
        // per monitor v2 is the awareness the metrics read scale factors under, which a manifest would set (AD-27)
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        // the loop factory builds the win32 backed loop, which an application runs on (§5.3, AD-16)
        set_platform_event_loop_factory([] { return std::unique_ptr<PlatformEventLoop>(new Win32EventLoop()); });
        // the window factory builds the win32 window that hosts the render surface (§5.3, AD-16)
        set_platform_window_factory([](const WindowOptions& options) -> std::unique_ptr<PlatformWindow> {
            // the factory returns the concrete window as the hal, so no win32 type escapes the module (§3.5)
            return Win32PlatformWindow::create(options);
        });
    }
} // namespace albor::windows
