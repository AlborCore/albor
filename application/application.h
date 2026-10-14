#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "application/task_queue.h"
#include "application/timer.h"
#include "application/window_stage.h"
#include "commands/command_registry.h"
#include "core/paint.h"
#include "ui/ui_event.h"
#include "window/system_appearance.h"
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

    // the state the installed factory lives in, heap held behind a constant initialized pointer (§4.1)
    struct PlatformEventLoopFactoryState
    {
        // the installed factory, which is empty until a platform backend installs one (§4.1)
        PlatformEventLoopFactory factory;
    };

    // install the factory the framework builds its native loop with
    void set_platform_event_loop_factory(PlatformEventLoopFactory factory);

    // hand out the installed factory, which is empty until a platform backend installs one
    [[nodiscard]] const PlatformEventLoopFactory& platform_event_loop_factory();

    class RendererBackend;
    class TextEngine;

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

        // create a main window whose tree the builder builds, wired for rendering (§6.1, §8.1)
        WindowHandle create_window(const WindowOptions& options, RootBuilder builder);

        // find the window a handle names, returning null when the handle is stale (§5.2)
        [[nodiscard]] Window* window(WindowHandle handle);

        // find the window a handle names for reading, returning null when the handle is stale (§5.2)
        [[nodiscard]] const Window* window(WindowHandle handle) const;

        // hand out the tree a main window drives, returning null when the handle is stale or has no tree (§6.1, §8.4)
        [[nodiscard]] UiNode* root(WindowHandle handle);

        // replace the colour a window's scene clears with, which a themed surface writes (§6.1, §8.4)
        void set_window_background(WindowHandle handle, Color background);

        // destroy the window a handle names, which closes its native window first (§5.2)
        bool destroy_window(WindowHandle handle);

        // hand out the number of open windows
        [[nodiscard]] std::size_t window_count() const;

        // hand out how many frames a window's tree built, which a window without one never did (§6.1)
        [[nodiscard]] std::uint64_t frame_count(WindowHandle handle) const;

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

        // install the callback a Closed notification reaches, which is how an application hears a window is gone (§5.2)
        void set_closed_handler(std::function<void(WindowHandle)> handler);

        // install the callback a normalized input event reaches, which is how a window's input is driven (§8.3)
        void set_input_handler(std::function<void(WindowHandle, const Event&)> handler);

        // install the callback a system appearance change reaches, which is how a theme follows the system live (§8.4)
        void set_appearance_handler(std::function<void(WindowHandle, const SystemAppearance&)> handler);

        // report whether the application was asked to terminate
        [[nodiscard]] bool quit_requested() const;

        // hand out the exit code quit was called with
        [[nodiscard]] int exit_code() const;

        // report whether the application is currently running its loop
        [[nodiscard]] bool running() const;

        // hand out the commands this application shares across every window, which is one registry per run (§18.1)
        [[nodiscard]] CommandRegistry& commands();

    private:
        // attach the framework listeners to a window, which is what makes its events reach the application (§5.3)
        void bind_window(WindowHandle handle, Window& window);

        // build the renderer and the engine the main windows share, which only the first one pays for (§6.3, §9.1)
        bool ensure_engines();

        // drop the stage of a closed window, since nothing may touch its surface afterwards (§6.1)
        void release_stage(WindowHandle handle);

        // end the run when no window and no stage is left, which is the default lifetime (§4.1)
        void end_run_when_no_windows();

        // release the stages, the windows and the engines in the order a layer outlives its drawable (§6.3)
        void shutdown();

        // the commands every window of this run shares, which is one registry and one history (§18.1)
        CommandRegistry m_commands;

        // the native loop the application pumps, which is never null for a live application
        std::unique_ptr<PlatformEventLoop> m_event_loop;

        // the renderer and the text engine the main windows share, which the installed selection built (§6.3, §9.1)
        std::unique_ptr<RendererBackend> m_renderer;
        std::unique_ptr<TextEngine> m_text;

        // the frame machinery of every main window, keyed by the id of the window that owns it (§6.1, AD-18)
        std::map<std::uint64_t, std::unique_ptr<WindowStage>> m_stages;

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

        // the callback a Closed notification reaches per window, which the bind window forwards (§5.2)
        std::function<void(WindowHandle)> m_closed_handler;

        // the callback a normalized input event reaches, which the bind window forwards per window (§8.3)
        std::function<void(WindowHandle, const Event&)> m_input_handler;

        // the callback a system appearance change reaches, which the bind window forwards per window (§8.4)
        std::function<void(WindowHandle, const SystemAppearance&)> m_appearance_handler;

        // the exit code quit was called with, which run hands back (§4.1)
        int m_exit_code = 0;

        // whether the application was asked to terminate
        bool m_quit_requested = false;
    };
} // namespace albor
