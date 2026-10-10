#include "window/window_handle.h"

namespace albor
{
    // report whether the handle names a window at all
    bool WindowHandle::valid() const { return m_id != 0; }

    // hand out the identifier the handle carries
    std::uint64_t WindowHandle::id() const { return m_id; }

    // build a handle from an identifier, which only the window registry does
    WindowHandle WindowHandle::from_id(std::uint64_t id) { return WindowHandle(id); }

    // build a handle from an identifier, which the registry is a friend of
    WindowHandle::WindowHandle(std::uint64_t id) :
        m_id(id) {}
} // namespace albor
