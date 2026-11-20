#pragma once

#include <memory>
#include <string_view>

#include <xcb/xcb.h>

namespace albor::x11
{
    // the shared xcb connection, which one process holds one of per display name (§5.3, AD-05)
    class X11Connection
    {
    public:
        // close the connection, which the last window and loop release together (§10.5)
        ~X11Connection();

        X11Connection(const X11Connection&) = delete;
        X11Connection& operator=(const X11Connection&) = delete;
        X11Connection(X11Connection&&) = delete;
        X11Connection& operator=(X11Connection&&) = delete;

        // connect to the named display, reusing one connection per name, and report null when the host has none
        [[nodiscard]] static std::shared_ptr<X11Connection> connect(std::string_view name);

        // hand out the connection every request and event rides
        [[nodiscard]] xcb_connection_t* connection() const;

        // hand out the screen the default root belongs to
        [[nodiscard]] xcb_screen_t* screen() const;

        // hand out the descriptor the loop polls, which carries the events
        [[nodiscard]] int fd() const;

    private:
        // build a connection over the display the name selects
        explicit X11Connection(xcb_connection_t* connection, xcb_screen_t* screen);

        // the connection every request rides, which the adapter owns
        xcb_connection_t* m_connection = nullptr;

        // the screen the root window belongs to, which the setup owns
        xcb_screen_t* m_screen = nullptr;
    };
} // namespace albor::x11
