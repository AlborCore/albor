#pragma once

#include <cstdint>

namespace albor
{
    // a lightweight non owning handle to a live window (§5.2)
    class WindowHandle
    {
    public:
        // the default handle is the invalid handle, because it names no window
        WindowHandle() = default;

        // report whether the handle names a window at all, which says nothing about the window still being open
        [[nodiscard]] bool valid() const;

        // hand out the identifier the handle carries
        [[nodiscard]] std::uint64_t id() const;

        // build a handle from an identifier, which only the window registry does
        [[nodiscard]] static WindowHandle from_id(std::uint64_t id);

        // compare two handles by identity
        friend bool operator==(const WindowHandle& lhs, const WindowHandle& rhs) = default;

    private:
        // build a handle from an identifier, which the registry is a friend of
        explicit WindowHandle(std::uint64_t id);

        // the identifier of the window, where zero names no window
        std::uint64_t m_id = 0;
    };
} // namespace albor
