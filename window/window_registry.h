#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "window/window.h"
#include "window/window_handle.h"
#include "window/window_options.h"

namespace albor
{
    // the registry that owns every open window and names it by handle (§4.1, §5.2)
    class WindowRegistry
    {
    public:
        WindowRegistry() = default;

        WindowRegistry(const WindowRegistry&) = delete;
        WindowRegistry& operator=(const WindowRegistry&) = delete;
        WindowRegistry(WindowRegistry&&) = delete;
        WindowRegistry& operator=(WindowRegistry&&) = delete;
        ~WindowRegistry() = default;

        // create a window through the installed platform factory, returning the invalid handle when it fails
        [[nodiscard]] WindowHandle create(const WindowOptions& options);

        // adopt a window that already owns a native window, which is what a test double uses
        [[nodiscard]] WindowHandle adopt(std::unique_ptr<Window> window);

        // find the window a handle names, returning null when the handle is stale
        [[nodiscard]] Window* find(WindowHandle handle);

        // find the window a handle names for reading, returning null when the handle is stale
        [[nodiscard]] const Window* find(WindowHandle handle) const;

        // report whether a handle names a window that is still registered
        [[nodiscard]] bool alive(WindowHandle handle) const;

        // destroy the window a handle names, reporting whether it was there
        bool destroy(WindowHandle handle);

        // destroy every registered window, which application shutdown does
        void destroy_all();

        // hand out the number of registered windows
        [[nodiscard]] std::size_t size() const;

    private:
        // release the entry a handle names and report whether it was there
        bool release(WindowHandle handle);

        // the owned windows, indexed by identifier
        std::vector<std::unique_ptr<Window>> m_windows;

        // the identifier the next created window receives, which never reuses an old one
        std::uint64_t m_next_id = 1;
    };
} // namespace albor
