#include "window/window_registry.h"

namespace albor
{
    WindowHandle WindowRegistry::create(const WindowOptions& options) {
        // build the native window first, because a platform that refuses the options yields no window at all (§5.1)
        std::unique_ptr<PlatformWindow> platform = PlatformWindow::create(options);
        // a refused option set is reported as the invalid handle rather than a broken window (§5.1)
        if (!platform)
            return WindowHandle{};
        // a framework window wraps the native window and starts with the options it was created from
        std::unique_ptr<Window> window = std::make_unique<Window>(WindowHandle::from_id(m_next_id), options, std::move(platform));
        // the handle of the window is the identity the caller uses from here on (§5.2)
        const WindowHandle handle = window->handle();
        // the visibility of the options is applied before the window is registered (§5.1)
        if (options.visible)
            window->show();
        // the slot for the new identifier grows to fit, leaving any destroyed slot before it empty (§5.2)
        m_windows.resize(m_next_id);
        // the window takes its slot, which is what makes the handle resolve to it
        m_windows[m_next_id - 1] = std::move(window);
        // the next window receives a fresh identifier, so a destroyed handle never names a new window (§5.2)
        m_next_id++;
        return handle;
    }

    WindowHandle WindowRegistry::adopt(std::unique_ptr<Window> window) {
        // adopting nothing registers nothing, so the caller sees the invalid handle
        if (!window)
            return WindowHandle{};
        // the handle of the adopted window is the identity the caller uses from here on
        const WindowHandle handle = window->handle();
        // an adopted window may carry an identifier of its own, so the slots grow to fit it (§5.2)
        m_windows.resize(handle.id());
        // the window takes the slot its own identifier names
        m_windows[handle.id() - 1] = std::move(window);
        return handle;
    }

    Window* WindowRegistry::find(WindowHandle handle) {
        // the registry is the only source of handles, so a handle it never issued names no entry (§5.2)
        if (!handle.valid() || handle.id() > m_windows.size())
            return nullptr;
        // a destroyed window leaves an empty slot behind, which no handle resolves to (§5.2)
        return m_windows[handle.id() - 1].get();
    }

    const Window* WindowRegistry::find(WindowHandle handle) const {
        // the reading lookup resolves exactly what the writing one does (§5.2)
        return const_cast<WindowRegistry*>(this)->find(handle);
    }

    bool WindowRegistry::alive(WindowHandle handle) const {
        // a handle is alive exactly while it still resolves to a window (§5.2)
        return find(handle) != nullptr;
    }

    bool WindowRegistry::destroy(WindowHandle handle) {
        // the release step is what closes the native window before the window is dropped (§5.2)
        return release(handle);
    }

    void WindowRegistry::destroy_all() {
        // every open window closes before any entry is released, so the native windows go away in a defined order (§5.2)
        for (const std::unique_ptr<Window>& window : m_windows) {
            // a destroyed window leaves an empty slot behind, which has nothing left to close (§5.2)
            if (!window)
                continue;
            window->close();
        }
        // the entries are released only after every native window is closed
        m_windows.clear();
    }

    std::size_t WindowRegistry::size() const {
        std::size_t open = 0;
        // a destroyed window leaves an empty slot behind, which is not an open window (§5.2)
        for (const std::unique_ptr<Window>& window : m_windows) {
            if (window)
                open++;
        }
        return open;
    }

    bool WindowRegistry::release(WindowHandle handle) {
        // a stale handle has nothing to release (§5.2)
        Window* window = find(handle);
        if (window == nullptr)
            return false;
        // the native window closes before the framework window that owns it is dropped (§5.2)
        window->close();
        // the slot keeps its identifier but stops owning a window, so the handle stays stale forever (§5.2)
        m_windows[handle.id() - 1].reset();
        return true;
    }
} // namespace albor
