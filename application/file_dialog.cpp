#include "application/file_dialog.h"

#include <cstdio>
#include <optional>

#include "application/application.h"
#include "window/window.h"

namespace albor
{
    namespace
    {
        // the installed state, which a null pointer leaves empty without running any initializer (§5.3)
        PlatformFileDialogFactoryState* g_state = nullptr;

        // hand out the state, building it the first time it is asked for, which is the only writer (§5.3)
        [[nodiscard]] PlatformFileDialogFactoryState& state() {
            // the first caller creates the state, and every later one reads the same object (§5.3)
            if (g_state == nullptr)
                g_state = new PlatformFileDialogFactoryState();
            return *g_state;
        }
    } // namespace

    void set_platform_file_dialog_factory(PlatformFileDialogFactory factory) {
        // the factory replaces the previous one, because a process is served by exactly one backend (§5.3)
        state().factory = std::move(factory);
    }

    const PlatformFileDialogFactory& platform_file_dialog_factory() {
        // a never installed factory is the empty one, reported rather than crashed on (§5.3)
        static const PlatformFileDialogFactory empty;
        return g_state ? g_state->factory : empty;
    }

    std::optional<std::filesystem::path> Application::open_file(const OpenFileOptions& options) {
        // a process with no installed backend has no panel to show, which is reported rather than called a cancel (§5.3)
        const PlatformFileDialogFactory& factory = platform_file_dialog_factory();
        if (!factory) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: no file dialog backend is installed\n");
            return std::nullopt;
        }
        // the parent is resolved once, because both directions need the same rule (§5.1)
        NativeWindowHandle parent;
        // a parent that names no live window is refused, because an unowned panel would lie about its owner (§5.1)
        if (!resolve_file_dialog_parent(options.parent, parent)) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: the file dialog parent names no live window\n");
            return std::nullopt;
        }
        // the backend is built per call, because a panel is a transient surface (§5.3)
        std::unique_ptr<PlatformFileDialog> dialog = factory();
        // a backend that refuses to build a panel is reported rather than called a cancel (§5.3)
        if (!dialog) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: the file dialog backend built no panel\n");
            return std::nullopt;
        }
        // the panel runs on the ui thread, which is the thread that owns every native window (§5.1)
        return dialog->open(options, parent);
    }

    std::optional<std::filesystem::path> Application::save_file(const SaveFileOptions& options) {
        // a process with no installed backend has no panel to show, which is reported rather than called a cancel (§5.3)
        const PlatformFileDialogFactory& factory = platform_file_dialog_factory();
        if (!factory) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: no file dialog backend is installed\n");
            return std::nullopt;
        }
        // the parent is resolved once, because both directions need the same rule (§5.1)
        NativeWindowHandle parent;
        // a parent that names no live window is refused, because an unowned panel would lie about its owner (§5.1)
        if (!resolve_file_dialog_parent(options.parent, parent)) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: the file dialog parent names no live window\n");
            return std::nullopt;
        }
        // the backend is built per call, because a panel is a transient surface (§5.3)
        std::unique_ptr<PlatformFileDialog> dialog = factory();
        // a backend that refuses to build a panel is reported rather than called a cancel (§5.3)
        if (!dialog) {
            // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
            std::fprintf(stderr, "albor: the file dialog backend built no panel\n");
            return std::nullopt;
        }
        // the panel runs on the ui thread, which is the thread that owns every native window (§5.1)
        return dialog->save(options, parent);
    }

    bool Application::resolve_file_dialog_parent(WindowHandle handle, NativeWindowHandle& parent) const {
        // an invalid handle opens an unowned panel, which is a valid request (§5.3)
        parent = NativeWindowHandle{};
        // an unowned panel needs no lookup (§5.1)
        if (!handle.valid())
            return true;
        // the live window hands over the native object the platform needs (§5.1)
        const Window* owner = m_windows.find(handle);
        // a handle that names no window is stale, which the caller reports (§5.2)
        if (owner == nullptr)
            return false;
        // the native handle is opaque, which keeps the backend free of framework types (§3.5, AD-05)
        parent = owner->native_window_handle();
        return true;
    }
} // namespace albor
