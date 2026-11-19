#pragma once

#include <memory>
#include <mutex>

#include <xcb/xcb.h>

#include "albor/platform/linux/wsi_surface.h"

namespace albor::x11
{
    // the surface state a window shares with its renderer, which teardown clears first (§3.5, §5.3, §10.5)
    struct X11SurfaceState
    {
        // the lock that serializes the state and the surface requests of both threads (§10.5)
        std::mutex mutex;

        // the owner that keeps the connection alive while a render surface still holds the state (§10.5)
        std::shared_ptr<void> owner;

        // the connection the window lives on, which the wsi backend needs alongside the window
        xcb_connection_t* connection = nullptr;

        // the window the renderer attaches its buffers to, zeroed by teardown (§10.5)
        xcb_window_t window = 0;

        // the window teardown handed to the state, which the last holder destroys (§10.5)
        xcb_window_t pending_window = 0;

        // whether the window is mapped, which a hidden or dismissed window leaves false (§5.3)
        bool mapped = false;
    };

    // destroy the window a teardown handed over, which runs once the renderer released the state (§10.5)
    inline void destroy_surface_state(X11SurfaceState* state) {
        // the connection still stands, because the owner member outlives this call (§10.5)
        if (state->pending_window != 0 && state->connection != nullptr) {
            xcb_destroy_window(state->connection, state->pending_window);
            xcb_flush(state->connection);
        }
        delete state;
    }

    // the xcb objects a renderer surface attaches through, carried as one opaque handle (§3.5, AD-05)
    struct X11NativeSurface : linux_wsi::Surface
    {
        // the shared state the window clears before it destroys the window (§10.5)
        std::shared_ptr<X11SurfaceState> state;
    };
} // namespace albor::x11
