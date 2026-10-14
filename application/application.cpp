#include "application/application.h"

#include "render/renderer_backend.h"
#include "text/text_engine.h"

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§4.1)
        PlatformEventLoopFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§4.1)
        [[nodiscard]] PlatformEventLoopFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§4.1)
            if (g_state == nullptr)
                g_state = new PlatformEventLoopFactoryState();
            return *g_state;
        }

        std::unique_ptr<PlatformEventLoop> build_platform_event_loop() {
            // the reading factory is what the installed backend left behind (§4.1)
            const PlatformEventLoopFactory& factory = platform_event_loop_factory();
            // without an installed backend there is no native loop to run (§4.1)
            if (!factory)
                return nullptr;
            // the installed factory builds the loop this application runs on (§4.1)
            return factory();
        }
    } // namespace

    void set_platform_event_loop_factory(PlatformEventLoopFactory factory) {
        // the factory replaces the previous one, because a process is served by exactly one backend (§4.1)
        state().factory = std::move(factory);
    }

    const PlatformEventLoopFactory& platform_event_loop_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§4.1)
        static const PlatformEventLoopFactory empty;
        return g_state ? g_state->factory : empty;
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
        // an application that was never run still releases what it wired, in the same order (§4.1)
        shutdown();
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
        // the wiring is released here rather than at exit, where a static destructor runs too late (§4.1, §6.3)
        shutdown();
        // exit
        return m_exit_code;
    }

    void Application::shutdown() {
        // the stages release their surfaces before their windows are dropped, so a layer outlives its drawable (§6.1)
        m_stages.clear();
        // every window closes before the native loop goes away, because a window raises events into the loop (§5.2)
        m_windows.destroy_all();
        // the engines go after the surfaces that were drawn with them, so no frame outlives its context (§6.3, §9.1)
        m_text.reset();
        m_renderer.reset();
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

    WindowHandle Application::create_window(const WindowOptions& options, RootBuilder builder) {
        // the window joins the registry with the listeners every window carries, which keeps its events routed (§5.3)
        const WindowHandle handle = create_window(options);
        // the invalid handle only means the platform refused the window (§5.1)
        if (!handle.valid())
            return handle;
        // the renderer and the engine come from the installed selection, which the first main window builds (§6.3, §9.1)
        if (!ensure_engines()) {
            // the selection failed, so the window that could draw or measure nothing never shows (§6.3)
            m_windows.destroy(handle);
            return WindowHandle{};
        }
        // the stage wires the window's surface, pipeline, render thread and input once (§6.1, §8.3)
        std::unique_ptr<WindowStage> stage = std::make_unique<WindowStage>(*m_windows.find(handle), *m_renderer, *m_text, std::move(builder));
        // the window routes keys through the application's commands, which is what one shared object means (§18.5)
        stage->set_command_registry(&m_commands);
        // a stage the backend refused leaves nothing behind, because half a window cannot draw (§6.3)
        if (!stage->wired()) {
            std::fprintf(stderr, "application: the main window did not come up\n");
            m_windows.destroy(handle);
            return WindowHandle{};
        }
        // the stage lives as long as its window, which the closed notification drops (§6.1)
        m_stages[handle.id()] = std::move(stage);
        return handle;
    }

    CommandRegistry& Application::commands() {
        // the registry is one per application, so every window and every control reads the same object (§18.1)
        return m_commands;
    }

    Window* Application::window(WindowHandle handle) {
        // a stale handle resolves to nothing, which is how a caller detects a destroyed window (§5.2)
        return m_windows.find(handle);
    }

    const Window* Application::window(WindowHandle handle) const {
        // the reading lookup resolves exactly what the writing one does (§5.2)
        return m_windows.find(handle);
    }

    UiNode* Application::root(WindowHandle handle) {
        // a stale handle or a plain window has no tree, which is what null reports (§5.2)
        const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator stage = m_stages.find(handle.id());
        if (stage == m_stages.end())
            return nullptr;
        // the stage owns the built tree, which an application installs a theme over (§6.1, §8.4)
        return &stage->second->root();
    }

    void Application::set_window_background(WindowHandle handle, Color background) {
        // a stale handle has no scene to clear, which the stale window reports (§5.2)
        if (m_windows.find(handle) == nullptr)
            return;
        // the stage owns the scene background, which is what the frame clears with (§6.1, §8.4)
        const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator stage = m_stages.find(handle.id());
        if (stage != m_stages.end())
            stage->second->set_background(background);
    }

    bool Application::destroy_window(WindowHandle handle) {
        // the window's stage is dropped first, so its surface outlives no layer (§6.1)
        release_stage(handle);
        // the registry closes the native window before it drops the framework window (§5.2)
        if (!m_windows.destroy(handle))
            return false;
        // the teardown finished, so the Closed notification of §5.2 reaches the application
        if (m_closed_handler)
            m_closed_handler(handle);
        // an application that drained its last window ends its own run, which is the default lifetime (§4.1)
        end_run_when_no_windows();
        return true;
    }

    std::size_t Application::window_count() const {
        // a destroyed window leaves an empty slot behind, which is not an open window (§5.2)
        return m_windows.size();
    }

    std::uint64_t Application::frame_count(WindowHandle handle) const {
        // a window without a tree built nothing, which is what its count reports (§6.1)
        const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator entry = m_stages.find(handle.id());
        // a stage that never built reports zero (§6.1)
        return entry == m_stages.end() ? 0 : entry->second->frame_count();
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
        // every main window is driven by the same tick, which is what a headless loop reaches through (§6.1)
        for (const std::pair<const std::uint64_t, std::unique_ptr<WindowStage>>& entry : m_stages) {
            // the window may already be gone, whose stage dies with the closed notification (§5.2)
            const Window* window = m_windows.find(WindowHandle::from_id(entry.first));
            if (window == nullptr)
                continue;
            entry.second->run_frame(*window);
        }
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

    void Application::set_appearance_handler(std::function<void(WindowHandle, const SystemAppearance&)> handler) {
        // the handler replaces the previous one, so a change reaches exactly one owner (§8.4)
        m_appearance_handler = std::move(handler);
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
            // a main window answers its own resize, which is its stage's surface and tree (§6.1)
            const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator stage = m_stages.find(handle.id());
            if (stage != m_stages.end()) {
                stage->second->resized(*m_renderer, *m_windows.find(handle));
                return;
            }
            // a resize without an application handler is still recorded in the window metrics (§9.1)
            if (!m_resize_handler)
                return;
            // invoke the resize handler
            m_resize_handler(handle);
        });

        // the pacing tick of a window reaches the application frame handler, which is where drawing belongs (§4.3, §6.1)
        window.set_frame_handler([this, handle] {
            // a main window answers its own tick, which builds and submits its two stage frame (§6.1, AD-18)
            const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator stage = m_stages.find(handle.id());
            if (stage != m_stages.end()) {
                stage->second->run_frame(*m_windows.find(handle));
                return;
            }
            // a platform tick without a frame handler draws nothing (§6.1)
            if (!m_frame_handler)
                return;
            // invoke the frame handler
            m_frame_handler();
        });

        // a normalized input event of a window reaches the application input handler, which is where routing belongs (§8.3)
        window.set_input_handler([this, handle](const Event& event) {
            // a main window routes into its own tree, which is where the widgets answer (§8.3)
            const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator stage = m_stages.find(handle.id());
            if (stage != m_stages.end()) {
                Event routed = event;
                stage->second->deliver(routed);
                return;
            }
            // an event without an application handler is dropped, which is the quiet default of §8.3
            if (!m_input_handler)
                return;
            // the handle travels with the event, so the application can tell the windows apart (§8.3)
            m_input_handler(handle, event);
        });

        // a system appearance change reaches the appearance handler, which is where a theme follows the system (§8.4)
        window.set_appearance_handler([this, handle](const SystemAppearance& appearance) {
            // an application without an appearance handler records nothing, which is the quiet default (§8.4)
            if (!m_appearance_handler)
                return;
            // the handle travels with the appearance, so an application can tell the windows apart (§8.4)
            m_appearance_handler(handle, appearance);
        });

        // a native window close reaches the application as the Closed notification of §5.2
        window.set_closed_handler([this, handle] {
            // the teardown is never cancelled, because the window goes away whether or not the task runs (§5.2)
            [[maybe_unused]] const TaskHandle teardown = m_tasks.post([this, handle] {
                // the stage is dropped before the native window is, so its surface outlives no layer (§6.1)
                release_stage(handle);
                // the window was already torn down by an explicit destroy, which notified on its own (§5.2)
                if (!m_windows.destroy(handle))
                    return;
                // the teardown finished first, so the Closed notification follows it (§5.2)
                if (m_closed_handler)
                    m_closed_handler(handle);
                // an application that drained its last window ends its own run, which is the default lifetime (§4.1)
                end_run_when_no_windows();
            });
        });
    }

    bool Application::ensure_engines() {
        // the second main window reuses what the first built, because a backend is one per process (§6.2)
        if (m_renderer && m_text)
            return true;
        // the renderer comes from the installed selection, which an absent one reports through null (§6.3)
        std::unique_ptr<RendererBackend> renderer = create_renderer_backend();
        if (!renderer || !renderer->ready()) {
            std::fprintf(stderr, "application: the renderer backend did not come up\n");
            return false;
        }
        // the text engine comes from the installed selection, which an absent one reports through null (§9.1)
        std::unique_ptr<TextEngine> text = create_text_engine();
        if (!text || !text->ready()) {
            std::fprintf(stderr, "application: the text engine did not come up\n");
            return false;
        }
        // both are moved in together, because a half selection would betray the next window (§6.3)
        m_renderer = std::move(renderer);
        m_text = std::move(text);
        return true;
    }

    void Application::release_stage(WindowHandle handle) {
        // a stage whose window is closing is erased, so no handler reaches into dead wiring (§6.1)
        m_stages.erase(handle.id());
    }

    void Application::end_run_when_no_windows() {
        // a run with neither a window nor a stage has nothing to run, which is the default lifetime (§4.1)
        if (!m_stages.empty() || m_windows.size() != 0)
            return;
        // the run ends with the code quit already recorded, which is zero by default (§4.1)
        quit(m_exit_code);
    }
} // namespace albor
