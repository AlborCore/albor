#include "albor/application/application.h"

#include "albor/render/renderer_backend.h"
#include "albor/text/text_engine.h"

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
        // the host is pushed on the process stack, so a later application restores a live one, not a dead one (§5.1)
        push_window_popup_host(this);
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
        // a widget that outlives the application must not reach a dead host, so the live stack top returns (§5.1)
        remove_window_popup_host(this);
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
        // a native popup is released before the main stages, because its control owns the native handle it names (§5.1)
        std::vector<WindowHandle> popups;
        for (const std::pair<const std::uint64_t, std::unique_ptr<WindowStage>>& entry : m_stages) {
            // the stage belongs to a window, which is the only place the role is recorded (§5.1)
            const WindowHandle handle = WindowHandle::from_id(entry.first);
            const Window* window = m_windows.find(handle);
            // a popup has no application stage in the main tree, so it is the one released early (§5.1)
            if (window != nullptr && window->options().role == WindowRole::Popup)
                popups.push_back(handle);
        }
        // the popup stages drop before their native windows, so each surface precedes its drawable (§5.1, §6.1)
        for (const WindowHandle popup : popups) {
            // the stage is released for the same reason an ordinary close releases it first (§5.1)
            release_stage(popup);
            // the owner entry goes with the surface, which keeps the map exact until the application dies (§5.1)
            m_owned_windows.erase(popup.id());
            // the native popup is closed without a notification, because the application is going away (§5.2)
            (void)m_windows.destroy(popup);
        }
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
        const WindowHandle handle = m_windows.create(options, [this](Window& created) {
            // the application becomes the listener while the window is still off screen, so no event is missed (§5.3)
            bind_window(created.handle(), created, {}, false);
        });
        // a child names its owner, which is what the cascade and the modal gate walk (§5.1)
        if (handle.valid() && options.parent.valid())
            m_owned_windows[handle.id()] = options.parent;
        // a dialog takes the interaction when it opens, whether or not it drives a tree (§5.1)
        if (handle.valid())
            enter_dialog(handle, options);
        return handle;
    }

    WindowHandle Application::create_window(const WindowOptions& options, RootBuilder builder) { return create_window(options, std::move(builder), MainWindowConfig{}); }

    WindowHandle Application::create_window(const WindowOptions& options, RootBuilder builder, const MainWindowConfig& config) {
        // an ordinary window has no native close hook beyond the application's own notification (§5.2)
        return create_window_impl(options, std::move(builder), config, {}, false);
    }

    WindowHandle Application::create_window_impl(const WindowOptions& options, RootBuilder builder, const MainWindowConfig& config, std::function<void()> on_native_closed, bool keep_on_native_close) {
        // the window joins the registry with the listeners every window carries, which keeps its events routed (§5.3)
        const WindowHandle handle = m_windows.create(options, [this, on_native_closed = std::move(on_native_closed), keep_on_native_close](Window& created) {
            // the application becomes the listener while the window is still off screen, so no event is missed (§5.3)
            bind_window(created.handle(), created, on_native_closed, keep_on_native_close);
        });
        // the invalid handle only means the platform refused the window (§5.1)
        if (!handle.valid())
            return handle;
        // a child names its owner, which is what the cascade and the modal gate walk (§5.1)
        if (options.parent.valid())
            m_owned_windows[handle.id()] = options.parent;
        // the renderer and the engine come from the installed selection, which the first main window builds (§6.3, §9.1)
        if (!ensure_engines()) {
            // the selection failed, so the window that could draw or measure nothing never shows (§6.3)
            m_windows.destroy(handle);
            // the failed window leaves no ownership entry behind (§5.1)
            m_owned_windows.erase(handle.id());
            return WindowHandle{};
        }
        // the stage wires the window's surface, pipeline, render thread and input once (§6.1, §8.3)
        std::unique_ptr<WindowStage> stage = std::make_unique<WindowStage>(*m_windows.find(handle), *m_renderer, *m_text, std::move(builder), config);
        // the window routes keys through the application's commands, which is what one shared object means (§18.5)
        stage->set_command_registry(&m_commands);
        // an unclaimed escape asks the application to close, which is the one veto path of §5.2 (§8.3)
        stage->set_close_request([this, handle] {
            // the close is posted, because the stage that asked for it is still on the dispatch stack (§4.2)
            (void)m_tasks.post([this, handle] { (void)request_close(handle); });
        });
        // a stage the backend refused leaves nothing behind, because half a window cannot draw (§6.3)
        if (!stage->wired()) {
            std::fprintf(stderr, "application: the main window did not come up\n");
            m_windows.destroy(handle);
            // the failed window leaves no ownership entry behind (§5.1)
            m_owned_windows.erase(handle.id());
            return WindowHandle{};
        }
        // the stage lives as long as its window, which the closed notification drops (§6.1)
        m_stages[handle.id()] = std::move(stage);
        // a dialog takes the interaction when it opens, after its tree is wired and seeded (§5.1)
        enter_dialog(handle, options);
        return handle;
    }

    WindowHandle Application::open_popup(const UiNode& anchor, Rect anchor_rect, Size size, PopupRootBuilder builder, Color background, std::function<void()> on_closed) {
        // a popup without an owner has no parent window to float over, so it is refused quietly (§5.1)
        const WindowHandle owner = window_for_node(anchor);
        if (!owner.valid())
            return WindowHandle{};
        const Window* owner_window = m_windows.find(owner);
        if (owner_window == nullptr)
            return WindowHandle{};
        // the popup is a borderless transient owned by the trigger window, which is the native role of §5.1
        WindowOptions options;
        options.role = WindowRole::Popup;
        options.title = "Albor Popup";
        options.size = size;
        options.visible = false;
        options.resizable = false;
        options.parent = owner;
        options.anchor = anchor_rect;
        // the surface clears with the control's plate colour, because the popup owns no window background (§6.1, §8.4)
        MainWindowConfig config;
        config.background = background;
        // a popup keeps its tree when the native surface dismisses itself, so the control can reopen it (§5.1)
        const WindowHandle popup = create_window_impl(options, std::move(builder), config, std::move(on_closed), true);
        if (!popup.valid())
            return popup;
        // the owner is remembered, because a window teardown has to destroy the surfaces it anchors (§5.1)
        m_owned_windows[popup.id()] = owner;
        // a popup that was built off screen is shown once its first tree is ready, which avoids a blank flash (§5.1)
        if (Window* native = m_windows.find(popup))
            native->show();
        return popup;
    }

    void Application::show_popup(WindowHandle popup) {
        // only a transient surface has a show, so an ordinary window is left alone (§5.1)
        Window* window = m_windows.find(popup);
        if (window == nullptr || window->options().role != WindowRole::Popup)
            return;
        // the stage and its tree survived the dismissal, so showing is the whole reopen (§5.1)
        window->show();
    }

    bool Application::dismiss_owned_popups(WindowHandle owner) {
        // the popups are collected first, because closing one changes the state the walk reads (§5.1)
        std::vector<WindowHandle> popups;
        for (const std::pair<const std::uint64_t, WindowHandle>& entry : m_owned_windows) {
            // a child of another window is not this window's popup (§5.1)
            if (entry.second != owner)
                continue;
            // the entry names the child, which the registry resolves to the live surface (§5.2)
            const WindowHandle handle = WindowHandle::from_id(entry.first);
            const Window* window = m_windows.find(handle);
            // only a shown transient surface is dismissed, because a hidden one already is (§5.1)
            if (window == nullptr || window->options().role != WindowRole::Popup || !window->visible())
                continue;
            // the popup joins the collection, which the dismissal walks after the scan (§5.1)
            popups.push_back(handle);
        }
        // every collected popup hides through the one dismissal path, which keeps its tree alive (§5.1)
        for (const WindowHandle popup : popups)
            close_popup(popup);
        // a window with no shown popup owns nothing the key could dismiss (§5.1)
        return !popups.empty();
    }

    void Application::close_popup(WindowHandle popup) {
        // only a transient surface has a dismissal, so an ordinary window is left alone (§5.1)
        Window* window = m_windows.find(popup);
        if (window == nullptr || window->options().role != WindowRole::Popup)
            return;
        // hiding keeps the stage alive, and the hide path cancels the drag before it goes (§5.1, §8.3)
        window->hide();
    }

    void Application::destroy_popup(WindowHandle popup) {
        // only a transient surface is destroyed here, so an ordinary window is never closed by mistake (§5.1)
        const Window* window = m_windows.find(popup);
        if (window == nullptr || window->options().role != WindowRole::Popup)
            return;
        // the owner entry goes with the surface, because a stale owner would name a dead handle (§5.1)
        m_owned_windows.erase(popup.id());
        // an explicit teardown releases the stage before the native window, like any other window (§5.2, §6.1)
        destroy_window(popup);
    }

    void Application::destroy_owned_windows(WindowHandle owner) {
        // the owned windows are collected before any is destroyed, because the map changes during teardown (§5.1)
        std::vector<WindowHandle> transients;
        std::vector<WindowHandle> children;
        for (const std::pair<const std::uint64_t, WindowHandle>& entry : m_owned_windows) {
            // only a window that names this owner is part of its teardown (§5.1)
            if (entry.second != owner)
                continue;
            // a transient surface is anchored to the owner, so it goes before the window it floats over (§5.1)
            const WindowHandle child = WindowHandle::from_id(entry.first);
            const Window* window = m_windows.find(child);
            if (window != nullptr && window->options().role == WindowRole::Popup)
                transients.push_back(child);
            else
                children.push_back(child);
        }
        // every transient surface is destroyed before any dialog, because its anchor is gone with the owner (§5.1)
        for (const WindowHandle popup : transients)
            destroy_popup(popup);
        // every dialog is destroyed before the owner loses its native window, deepest first through its own cascade (§5.1)
        for (const WindowHandle child : children)
            destroy_window(child);
    }

    bool Application::descends_from(WindowHandle handle, WindowHandle ancestor) const {
        // a window with no owner to compare has no ancestry (§5.1)
        if (!ancestor.valid())
            return false;
        // the chain is walked from the window to the root, which is what nests a dialog under a dialog (§5.1)
        WindowHandle current = handle;
        while (current.valid()) {
            // the ancestor itself is part of its own subtree, which every modal rule needs (§5.1)
            if (current == ancestor)
                return true;
            // a window the map does not name is a root, so the chain ends there (§5.1)
            const auto owner = m_owned_windows.find(current.id());
            if (owner == m_owned_windows.end())
                return false;
            // the walk continues at the owner, which is one level up (§5.1)
            current = owner->second;
        }
        return false;
    }

    bool Application::input_blocked(WindowHandle handle) const {
        // a stale window receives no events, so there is nothing to block (§5.2)
        if (m_windows.find(handle) == nullptr)
            return false;
        // the first modal that constrains the window decides, which is the one a client feels (§5.1)
        bool blocked = false;
        m_windows.for_each([this, &blocked, handle](Window& blocking) {
            // a window already blocked by another modal needs no second answer (§5.1)
            if (blocked)
                return;
            // a window is never blocked by itself (§5.1)
            if (blocking.handle() == handle)
                return;
            // a transient surface never constrains another window, because it is not a top level surface (§5.1)
            if (!blocking.options().is_top_level())
                return;
            // a window with no modality constrains nothing (§5.1)
            if (blocking.options().modality == Modality::None)
                return;
            // a modal only constrains windows that predate it, which keeps the newest modal interactive (§5.1)
            if (handle.id() > blocking.handle().id())
                return;
            // a modal never blocks itself or a window that descends from it, which is what nests dialogs (§5.1)
            if (descends_from(handle, blocking.handle()))
                return;
            // an application modal window blocks every other window of the application (§5.1)
            if (blocking.options().modality == Modality::ApplicationModal)
                blocked = true;
            // a window modal window blocks its parent and everything under that parent (§5.1)
            else if (descends_from(handle, blocking.options().parent))
                blocked = true;
        });
        return blocked;
    }

    void Application::enter_dialog(WindowHandle handle, const WindowOptions& options) {
        // a modal window blocks the windows that predate it, so a press they held never sees its release (§8.3)
        if (options.modality != Modality::None) {
            for (const std::pair<const std::uint64_t, std::unique_ptr<WindowStage>>& entry : m_stages) {
                // a window the modal does not constrain keeps its drag, because its release still arrives (§5.1)
                if (input_blocked(WindowHandle::from_id(entry.first)))
                    cancel_stage_capture(WindowHandle::from_id(entry.first), PointerCancelReason::CaptureLost);
            }
        }
        // only a dialog takes the interaction on open, because an ordinary window does not steal focus (§5.1)
        if (options.role != WindowRole::Dialog)
            return;
        // the parent control is remembered before the dialog takes the interaction (§8.3)
        remember_invoking_focus(handle, options.parent);
        // a hidden dialog is seeded before it is shown, so it takes key status when the application shows it (§5.1)
        Window* dialog = m_windows.find(handle);
        if (dialog != nullptr && dialog->visible())
            dialog->activate();
    }

    void Application::remember_invoking_focus(WindowHandle dialog, WindowHandle parent) {
        // a dialog with no parent has no control to give focus back to (§5.1)
        WindowStage* parent_stage = stage(parent);
        if (parent_stage == nullptr)
            return;
        // the focused control is what opened the dialog, which closing returns to (§8.3)
        const UiNode* focused = parent_stage->focus().focused();
        if (focused == nullptr)
            return;
        // the identity is what survives a rebuild of the parent tree, so a pointer is not remembered (AD-11)
        const std::string& id = focused->identity().explicit_id;
        if (id.empty())
            return;
        // the identity is remembered under the dialog, which is the key the close looks it up by (§5.1)
        m_return_focus[dialog.id()] = id;
    }

    void Application::restore_invoking_focus(WindowHandle dialog, WindowHandle parent) {
        // a dialog that remembered no control gives focus back to nothing (§5.1)
        const auto stored = m_return_focus.find(dialog.id());
        if (stored == m_return_focus.end())
            return;
        // the identity is taken before the entry is dropped, because the erase invalidates the iterator (§5.1)
        const std::string id = stored->second;
        m_return_focus.erase(stored);
        // the parent stage owns the tree the control lives in (§6.1)
        WindowStage* parent_stage = stage(parent);
        if (parent_stage == nullptr)
            return;
        // the control is found again by identity, because the parent tree may have been rebuilt (§18.1)
        UiNode* target = find_by_explicit_id(parent_stage->root(), id);
        if (target != nullptr)
            parent_stage->focus().request_focus(target);
    }

    UiNode* Application::find_by_explicit_id(UiNode& root, std::string_view id) {
        // the first node carrying the identity is the control, because an author id is unique (AD-11)
        UiNode* found = nullptr;
        root.visit([&found, id](UiNode& node) {
            // a match already found is not replaced, which keeps the first node in tree order (AD-11)
            if (found == nullptr && node.identity().explicit_id == id)
                found = &node;
        });
        return found;
    }

    WindowHandle Application::window_for_node(const UiNode& node) const {
        // the tree walks to its root, which is the node every stage owns (§8.1)
        const UiNode* root = &node;
        while (root->parent() != nullptr)
            root = root->parent();
        // the stage that owns the root names the window a popup is parented to (§5.1)
        for (const std::pair<const std::uint64_t, std::unique_ptr<WindowStage>>& entry : m_stages) {
            if (&entry.second->root() == root)
                return WindowHandle::from_id(entry.first);
        }
        // a node that belongs to no stage has no owner window (§5.1)
        return WindowHandle{};
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
        // a close inside a frame pass or an input dispatch is recorded, because that pass is reading it (§4.2)
        if (m_driving_frames || m_dispatch_depth > 0) {
            m_deferred.push_back(handle);
            return true;
        }
        // the teardown is bracketed, because a dialog destroyed with its owner must not reactivate it (§5.1)
        m_teardown_depth++;
        // a child window never outlives the window that owns it, so its teardown comes first (§5.1)
        destroy_owned_windows(handle);
        // the role and the owner are read before the window is dropped, because both die with it (§5.1)
        const Window* closing = m_windows.find(handle);
        const bool popup = closing != nullptr && closing->options().role == WindowRole::Popup;
        const bool dialog = closing != nullptr && closing->options().role == WindowRole::Dialog;
        const WindowHandle parent = dialog ? closing->options().parent : WindowHandle{};
        // the owner entry goes with the window, which keeps the map exact (§5.1)
        m_owned_windows.erase(handle.id());
        // the window's stage is dropped first, so its surface outlives no layer (§6.1)
        release_stage(handle);
        // the registry closes the native window before it drops the framework window (§5.2)
        if (!m_windows.destroy(handle)) {
            m_teardown_depth--;
            return false;
        }
        // a dialog gives activation and focus back only when its owner is not the window being closed (§5.1, §8.3)
        if (dialog && m_teardown_depth == 1) {
            restore_invoking_focus(handle, parent);
            // a hidden owner stays hidden, because a later show seeds it before it takes key status (§5.1)
            if (Window* owner = m_windows.find(parent); owner != nullptr && owner->visible())
                owner->activate();
        }
        // the teardown finished, so the Closed notification of §5.2 reaches the application for a window it owns
        if (!popup && m_closed_handler)
            m_closed_handler(handle);
        // the teardown is done before the run is asked whether it ended (§4.1)
        m_teardown_depth--;
        // an application that drained its last window ends its own run, which is the default lifetime (§4.1)
        end_run_when_no_windows();
        return true;
    }

    bool Application::request_close(WindowHandle handle) {
        // a stale handle has nothing to close (§5.2)
        Window* window = m_windows.find(handle);
        if (window == nullptr)
            return false;
        // the close handler may veto the request, which is the first step of §5.2
        if (!window->on_platform_close_request())
            return false;
        // a granted request tears the window down through the one teardown path (§5.2)
        return destroy_window(handle);
    }

    std::size_t Application::window_count() const {
        // a destroyed window leaves an empty slot behind, which is not an open window (§5.2)
        return m_windows.size();
    }

    std::uint64_t Application::frame_count(WindowHandle handle) const {
        // a window without a tree built nothing, which is what its count reports (§6.1)
        const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator entry = m_stages.find(handle.id());
        // a stage that never built reports zero (§6.1)
        return entry == m_stages.end() ? 0u : entry->second->frame_count();
    }

    WindowStage* Application::stage(WindowHandle handle) {
        // the map is keyed by the window id, which is what a handle names (§5.2)
        const std::map<std::uint64_t, std::unique_ptr<WindowStage>>::const_iterator entry = m_stages.find(handle.id());
        // a window that is not a main window has no stage, which null reports (§6.1)
        return entry == m_stages.end() ? nullptr : entry->second.get();
    }

    bool Application::set_frame_hook(WindowHandle handle, std::function<void()> hook) {
        // a window that is not a main window has no stage to install into (§6.1)
        WindowStage* found = stage(handle);
        if (found == nullptr)
            return false;
        // the stage runs the hook once a scene was built and before stage two executes it (§6.1)
        found->set_frame_hook(std::move(hook));
        return true;
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
        // queued work runs before timers, so a posted task observes the current instant (§4.1)
        m_tasks.drain();
        // the due timers run next, because a timer callback may post further work (§4.2)
        m_timers.fire_due();
        // a frame tick only draws when something asked for a frame (§6.1)
        if (m_frame_handler)
            m_frame_handler();
        // the flag makes a teardown that arrives inside the pass wait for it, so no window is read after release (§4.2)
        m_driving_frames = true;
        for (const std::pair<const std::uint64_t, std::unique_ptr<WindowStage>>& entry : m_stages) {
            // the window may already be gone, whose stage dies with the closed notification (§5.2)
            const Window* window = m_windows.find(WindowHandle::from_id(entry.first));
            if (window == nullptr)
                continue;
            entry.second->run_frame(*window);
        }
        m_driving_frames = false;
        // a teardown that arrived while the pass was running is applied now, which is after the walk ended (§4.2)
        drain_deferred_stages();
    }

    void Application::drain_deferred_stages() {
        // nothing was deferred, which is the ordinary tick and the one that must not allocate (§4.2)
        if (m_deferred.empty())
            return;
        // the walk is driven by index, because a teardown may append the window it closed in turn (§4.2)
        for (std::size_t index = 0; index < m_deferred.size(); ++index)
            destroy_window(m_deferred[index]);
        // the queue is emptied last, because a teardown may have appended to it (§4.2)
        m_deferred.clear();
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

    void Application::cancel_stage_capture(WindowHandle handle, PointerCancelReason reason) {
        // a stale handle or a window without a stage has no capture to cancel (§5.2, §8.3)
        WindowStage* routed_to = stage(handle);
        if (routed_to == nullptr)
            return;
        // the cancel dispatches application code, so a teardown it asks for waits for the walk to unwind (§4.2)
        m_dispatch_depth++;
        routed_to->cancel_pointer_capture(reason);
        m_dispatch_depth--;
        // a teardown a handler requested is posted, because the window that raised the cancel is still on its stack (§4.2)
        if (m_dispatch_depth == 0 && !m_deferred.empty())
            (void)m_tasks.post([this] { drain_deferred_stages(); });
    }

    bool Application::deliver_input(WindowHandle handle, Event& event) {
        // a window an open modal constrains receives no input, whoever delivers it (§5.1, AD-08)
        if (input_blocked(handle))
            return false;
        // a window without a stage has no tree to route into (§6.1)
        WindowStage* routed_to = stage(handle);
        if (routed_to == nullptr)
            return false;
        // the dispatch is bracketed, because a handler may ask to tear its window down (§4.2)
        m_dispatch_depth++;
        routed_to->deliver(event);
        m_dispatch_depth--;
        // a teardown a handler requested is posted, because the window that raised the event is still on its stack (§4.2)
        if (m_dispatch_depth == 0 && !m_deferred.empty())
            (void)m_tasks.post([this] { drain_deferred_stages(); });
        return true;
    }

    void Application::bind_window(WindowHandle handle, Window& window, std::function<void()> on_native_closed, bool keep_on_native_close) {
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

        // the pacing tick reaches the application frame handler, which is where drawing belongs (§4.3, §6.1)
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

        // a window that loses key status cannot release its drag, so the drag ends and the cue hides (§5.1, §8.3)
        window.set_focus_lost_handler([this, handle] {
            // the capture drops first, because the window will never receive its release (§8.3)
            cancel_stage_capture(handle, PointerCancelReason::CaptureLost);
            // the stage hides the cue while the window is inactive, which keeps the focus owner (§8.3)
            if (WindowStage* owner_stage = stage(handle); owner_stage != nullptr)
                owner_stage->set_window_active(false);
        });
        // a window that regains key status restores the cue its focus owner carried (§5.1, §8.3)
        window.set_focus_gained_handler([this, handle] {
            // the stage restores the cue without moving the owner, which is the pair the loss opened (§8.3)
            if (WindowStage* owner_stage = stage(handle); owner_stage != nullptr)
                owner_stage->set_window_active(true);
        });
        // a window that leaves the screen cancels the drag before it hides (§5.1, §8.3)
        window.set_hidden_handler([this, handle] { cancel_stage_capture(handle, PointerCancelReason::WindowHidden); });
        // a normalized input event reaches the application input handler, where routing belongs (§8.3)
        window.set_input_handler([this, handle](const Event& event) {
            // a window an open modal constrains receives no input at all, which is what modality means (§5.1, AD-08)
            if (input_blocked(handle))
                return;
            // a main window routes into its own tree, which is where the widgets answer (§8.3)
            Event routed = event;
            // a window with a stage routes through the bracketed delivery, which defers a handler's teardown (§4.2)
            const bool delivered = deliver_input(handle, routed);
            // an unclaimed escape dismisses the popups the window owns, which is the keyboard dismissal (§5.1, §8.3)
            if (routed.kind == EventKind::KeyDown && routed.key == Key::Escape && !routed.handled && dismiss_owned_popups(handle))
                return;
            if (delivered) {
                // a main window is observed too, because a host reacts to what its tree answered (§8.3, AD-24)
                if (m_input_handler)
                    m_input_handler(handle, routed);
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
        window.set_closed_handler([this, handle, on_native_closed = std::move(on_native_closed), keep_on_native_close] {
            // a popup dismisses itself without ending its tree, so the owner is told and the stage stays alive (§5.1)
            if (keep_on_native_close) {
                // the native surface left the screen, so its input cancels the drag it cannot release (§8.3)
                cancel_stage_capture(handle, PointerCancelReason::WindowHidden);
                if (on_native_closed)
                    on_native_closed();
                return;
            }
            // the teardown is never cancelled, because the window goes away whether or not the task runs (§5.2)
            [[maybe_unused]] const TaskHandle teardown = m_tasks.post([this, handle] {
                // the one teardown cascades to the children, restores the parent and notifies (§5.1, §5.2)
                (void)destroy_window(handle);
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
            // the family is named in the failure, because it is the fact an application must act on (§9.1, AD-34)
            std::fprintf(stderr, "application: the text engine did not come up for family '%s'\n", installed_font_family().c_str());
            return false;
        }
        // both are moved in together, because a half selection would betray the next window (§6.3)
        m_renderer = std::move(renderer);
        m_text = std::move(text);
        return true;
    }

    void Application::release_stage(WindowHandle handle) {
        // a stage whose window is closing is erased, because no handler may reach into dead wiring (§6.1)
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
