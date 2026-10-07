#include "application/application.h"

namespace albor
{
    namespace
    {
        // the installed native loop factory, which is empty until a platform backend installs one (§4.1)
        PlatformEventLoopFactory g_event_loop_factory;

        std::unique_ptr<PlatformEventLoop> build_platform_event_loop() {
            // without an installed backend there is no native loop to run (§4.1)
            if (!g_event_loop_factory)
                return nullptr;
            // the installed factory builds the loop this application runs on (§4.1)
            return g_event_loop_factory();
        }
    } // namespace

    void set_platform_event_loop_factory(PlatformEventLoopFactory factory) {
        // the factory replaces the previous one, because a process is served by exactly one backend (§4.1)
        g_event_loop_factory = std::move(factory);
    }

    const PlatformEventLoopFactory& platform_event_loop_factory() {
        // an uninstalled backend is reported as an empty factory rather than a crash (§4.1)
        return g_event_loop_factory;
    }

    Application::Application(std::unique_ptr<PlatformEventLoop> event_loop) :
        m_event_loop(std::move(event_loop)) {
        // an application without a native loop still pumps tasks and timers, but it can never run (§4.1)
        if (m_event_loop) {
            // the loop pumps the ui thread, which is what turns posted work into execution (§4.1)
            m_event_loop->set_pump_handler([this] { pump(); });
        }
    }

    Application::Application() :
        Application(build_platform_event_loop()) {}

    Application::~Application() {
        // every window closes before the native loop goes away, because a window raises events into the loop (§5.2)
        m_windows.destroy_all();
        // the pacing source is stopped last, so no frame tick arrives after the windows are gone (§4.3)
        if (m_event_loop)
            m_event_loop->stop_frame_pacing();
    }

    Application& Application::instance() {
        // the application is built on first use, so a platform backend installs its factory before main runs (§4.1)
        static Application application;
        // use static storage to ensure a single instance throughout the application's lifetime (§4.1)
        return application;
    }

    int Application::run() {
        // without a native loop there is nothing to run, so the exit code is reported immediately (§4.1)
        if (!m_event_loop)
            return m_exit_code;
        // the pacing source drives frame ticks, which is how the loop reaches a steady sixty hertz (§4.3, AD-16)
        m_event_loop->start_frame_pacing();
        // the native loop blocks the calling thread until quit is called (§4.1)
        m_event_loop->run();
        // exit
        return m_exit_code;
    }

    void Application::quit(int exit_code) {
        // the flag is set before the loop is asked to stop, so a nested loop still observes the request (§4.3)
        m_quit_requested = true;
        m_exit_code = exit_code;
        // an application without a native loop has nothing to stop, but the request is still recorded (§4.1)
        if (!m_event_loop)
            return;
        // the loop stop itself is thread safe, so a worker thread may terminate the application (§4.1)
        m_event_loop->quit();
    }

    TaskHandle Application::post(Task task) {
        // the task joins the queue the next pump will drain (§4.1)
        return m_tasks.post(std::move(task));
    }

    TaskHandle Application::post(Task task, const LifetimeToken& guard) {
        // the guard is carried with the task, so the pump can drop it once the source expired (§4.2)
        return m_tasks.post_guarded(std::move(task), guard);
    }

    WindowHandle Application::create_window(const WindowOptions& options) {
        // the registry builds the native window, and the hook binds the listeners before the window is shown (§5.3)
        return m_windows.create(options, [this](Window& created) {
            // the application becomes the listener while the window is still off screen, so no event is missed (§5.3)
            bind_window(created.handle(), created);
        });
    }

    Window* Application::window(WindowHandle handle) {
        // a stale handle resolves to nothing, which is how a caller detects a destroyed window (§5.2)
        return m_windows.find(handle);
    }

    const Window* Application::window(WindowHandle handle) const {
        // the reading lookup resolves exactly what the writing one does (§5.2)
        return m_windows.find(handle);
    }

    bool Application::destroy_window(WindowHandle handle) {
        // the registry closes the native window before it drops the framework window (§5.2)
        if (!m_windows.destroy(handle))
            return false;
        // the teardown finished, so the Closed notification of §5.2 reaches the application
        if (m_closed_handler)
            m_closed_handler(handle);
        return true;
    }

    std::size_t Application::window_count() const {
        // a destroyed window leaves an empty slot behind, which is not an open window (§5.2)
        return m_windows.size();
    }

    TaskQueue& Application::tasks() {
        // the queue is the one the pump drains, so the caller posts into the live one (§4.1)
        return m_tasks;
    }

    TimerScheduler& Application::timers() {
        // the scheduler is the one the pump fires, so the caller arms on the live one (§4.2)
        return m_timers;
    }

    PlatformEventLoop& Application::event_loop() {
        // the loop is the one the constructor installed, which is the one the application pumps (§4.1)
        return *m_event_loop;
    }

    void Application::pump() {
        // the wake flag is cleared first, so a task posted during this pass wakes the loop again (§4.1)
        m_tasks.acknowledge_wake();
        // queued work runs before timers, so a posted task observes the state of the current instant (§4.1)
        m_tasks.drain();
        // the due timers run next, because a timer callback may post further work (§4.2)
        m_timers.fire_due();
        // a frame tick only draws when something asked for a frame (§6.1)
        if (m_frame_handler)
            m_frame_handler();
    }

    void Application::set_frame_handler(std::function<void()> handler) {
        // the handler is replaced rather than chained, so a window never draws twice per tick (§6.1)
        m_frame_handler = std::move(handler);
    }

    void Application::set_resize_handler(std::function<void(WindowHandle)> handler) {
        // the handler is replaced rather than chained, so a resize reaches exactly one owner (§6.1)
        m_resize_handler = std::move(handler);
    }

    void Application::set_close_handler(std::function<bool(WindowHandle)> handler) {
        // the handler may veto, so it is consulted on every close request (§5.2)
        m_close_handler = std::move(handler);
    }

    void Application::set_closed_handler(std::function<void(WindowHandle)> handler) {
        // the handler is replaced rather than chained, so a close reaches exactly one owner (§5.2)
        m_closed_handler = std::move(handler);
    }

    void Application::set_input_handler(std::function<void(WindowHandle, const Event&)> handler) {
        // the handler replaces the previous one, so the new owner receives every wake of §8.3
        m_input_handler = std::move(handler);
    }

    bool Application::quit_requested() const {
        // the request is recorded even when the application never reached its loop (§4.1)
        return m_quit_requested;
    }

    int Application::exit_code() const {
        // the code is whatever quit was called with, which run hands back (§4.1)
        return m_exit_code;
    }

    bool Application::running() const {
        // an application without a native loop never runs (§4.1)
        if (!m_event_loop)
            return false;
        // check event loop status
        return m_event_loop->running();
    }

    void Application::bind_window(WindowHandle handle, Window& window) {
        // a window whose close is vetoed by the application stays alive, so the request only ends there (§5.2)
        window.set_close_handler([this, handle] {
            // without an application handler the framework grants the close, which is the default (§5.2)
            if (!m_close_handler)
                return true;
            // invoke the close handler
            return m_close_handler(handle);
        });

        // a resize reaches the application with the handle, so it can find its own state (§6.1)
        window.set_resize_handler([this, handle](const WindowMetrics&) {
            // a resize without an application handler is still recorded in the window metrics (§9.1)
            if (!m_resize_handler)
                return;
            // invoke the resize handler
            m_resize_handler(handle);
        });

        // the pacing tick of a window reaches the application frame handler, which is where drawing belongs (§4.3, §6.1)
        window.set_frame_handler([this] {
            // a platform tick without a frame handler draws nothing (§6.1)
            if (!m_frame_handler)
                return;
            // invoke the frame handler
            m_frame_handler();
        });

        // a normalized input event of a window reaches the application input handler, which is where routing belongs (§8.3)
        window.set_input_handler([this, handle](const Event& event) {
            // an event without an application handler is dropped, which is the quiet default of §8.3
            if (!m_input_handler)
                return;
            // the handle travels with the event, so the application can tell the windows apart (§8.3)
            m_input_handler(handle, event);
        });

        // a native window close reaches the application as the Closed notification of §5.2
        window.set_closed_handler([this, handle] {
            // the teardown is posted, so it never runs inside the platform close callback (§4.1, §5.2)
            m_tasks.post([this, handle] {
                // the window was already torn down by an explicit destroy, which notified on its own (§5.2)
                if (!m_windows.destroy(handle))
                    return;
                // the teardown finished first, so the Closed notification follows it (§5.2)
                if (m_closed_handler)
                    m_closed_handler(handle);
            });
        });
    }
} // namespace albor
