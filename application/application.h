#pragma once

#include <functional>
#include <memory>

#include "application/task_queue.h"
#include "application/timer.h"
#include "window/window.h"
#include "window/window_handle.h"
#include "window/window_options.h"
#include "window/window_registry.h"

namespace albor
{
    // the native loop the application runs on, which a platform backend implements (§4.1, AD-16)
    class PlatformEventLoop
    {
    public:
        virtual ~PlatformEventLoop() = default;

        // install the callback the loop invokes once per iteration, which is how the ui thread is pumped (§4.1)
        virtual void set_pump_handler(Task pump) = 0;

        // run the native loop until quit is called, which blocks the calling thread
        virtual void run() = 0;

        // ask the native loop to stop, which is thread safe (§4.1)
        virtual void quit() = 0;

        // report whether the native loop is running
        [[nodiscard]] virtual bool running() const = 0;

        // start the pacing source that raises frame ticks, which a display link drives (§4.3, AD-16)
        virtual void start_frame_pacing() = 0;

        // stop the pacing source, which a window teardown requires (§4.3)
        virtual void stop_frame_pacing() = 0;
    };

    // the signature a platform backend implements to build the native loop
    using PlatformEventLoopFactory = std::function<std::unique_ptr<PlatformEventLoop>()>;

    // install the factory the framework builds its native loop with
    void set_platform_event_loop_factory(PlatformEventLoopFactory factory);

    // hand out the installed factory, which is empty until a platform backend installs one
    [[nodiscard]] const PlatformEventLoopFactory& platform_event_loop_factory();

    // the central singleton that owns the native loop, the window registry and the task dispatcher (§4.1)
    class Application
    {
    public:
        // build an application over a native loop the caller owns, which a test substitutes (§4.1)
        explicit Application(std::unique_ptr<PlatformEventLoop> event_loop);

        // the default application uses the loop the installed factory builds (§4.1)
        Application();

        // stop the pacing source and close every window in a defined order (§4.1)
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        // hand out the process wide application, which the default constructor creates on first use (§4.1)
        static Application& instance();

        // run the native loop until quit is called and hand out the exit code (§4.1)
        int run();

        // request application termination, which is thread safe (§4.1)
        void quit(int exit_code = 0);

        // post a task to the UI thread, which is thread safe (§4.1)
        TaskHandle post(Task task);

        // post a guarded task, which is dropped when its token expired before the task ran (§4.2)
        TaskHandle post(Task task, const LifetimeToken& guard);

        // create a window on the UI thread and hand out the handle that names it (§4.1)
        WindowHandle create_window(const WindowOptions& options);

        // find the window a handle names, returning null when the handle is stale (§5.2)
        [[nodiscard]] Window* window(WindowHandle handle);

        // find the window a handle names for reading, returning null when the handle is stale (§5.2)
        [[nodiscard]] const Window* window(WindowHandle handle) const;

        // destroy the window a handle names, which closes its native window first (§5.2)
        bool destroy_window(WindowHandle handle);

        // hand out the number of open windows
        [[nodiscard]] std::size_t window_count() const;

        // hand out the task dispatcher, which an application posts work to
        [[nodiscard]] TaskQueue& tasks();

        // hand out the timer registry, which an application arms ui thread timers on (§4.2)
        [[nodiscard]] TimerScheduler& timers();

        // hand out the native loop the application runs on
        [[nodiscard]] PlatformEventLoop& event_loop();

        // pump the queued tasks, the due timers and the frame ticks once, which is one loop iteration (§4.1)
        void pump();

        // install the callback every frame tick reaches, which is how an application draws (§6.1)
        void set_frame_handler(std::function<void()> handler);

        // install the callback a native window resize reaches, which is how an application remeasures (§6.1)
        void set_resize_handler(std::function<void(WindowHandle)> handler);

        // install the callback a native close request reaches, which may veto the close (§5.2)
        void set_close_handler(std::function<bool(WindowHandle)> handler);

        // report whether the application was asked to terminate
        [[nodiscard]] bool quit_requested() const;

        // hand out the exit code quit was called with
        [[nodiscard]] int exit_code() const;

        // report whether the application is currently running its loop
        [[nodiscard]] bool running() const;

    private:
        // attach the framework listeners to a window, which is what makes its events reach the application (§5.3)
        void bind_window(WindowHandle handle, Window& window);

        // the native loop the application pumps, which is never null for a live application
        std::unique_ptr<PlatformEventLoop> m_event_loop;

        // the task queue that carries work from any thread to the ui thread (§4.1)
        TaskQueue m_tasks;

        // the registry that fires the due ui thread timers (§4.2)
        TimerScheduler m_timers;

        // the registry that owns every open window (§4.1)
        WindowRegistry m_windows;

        // the callback every frame tick reaches (§6.1)
        std::function<void()> m_frame_handler;

        // the callback a native window resize reaches (§6.1)
        std::function<void(WindowHandle)> m_resize_handler;

        // the callback a native close request reaches, which may veto the close (§5.2)
        std::function<bool(WindowHandle)> m_close_handler;

        // the exit code quit was called with, which run hands back (§4.1)
        int m_exit_code = 0;

        // whether the application was asked to terminate
        bool m_quit_requested = false;
    };
} // namespace albor
