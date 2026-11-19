#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <wayland-client.h>

#include "albor/platform/linux/wsi_surface.h"

namespace albor::wayland
{
    // the surface state a window shares with its renderer, which teardown clears first (§3.5, §5.3, §10.5)
    struct WaylandSurfaceState
    {
        // the lock that serializes the state and the surface requests of both threads (§10.5)
        std::mutex mutex;

        // the owner that keeps the display alive while a render surface still holds the state (§10.5)
        std::shared_ptr<void> owner;

        // the display the surface belongs to, which the wsi backend needs alongside the surface
        wl_display* display = nullptr;

        // the surface the renderer attaches its buffers to, nulled by teardown before it is destroyed (§10.5)
        wl_surface* surface = nullptr;

        // the surface teardown handed to the state, which the last holder destroys (§10.5)
        wl_surface* pending_surface = nullptr;

        // the shm pool a cpu renderer carves its presentation buffers from (§6.3, AD-26)
        wl_shm* shm = nullptr;

        // whether the window is mapped, which a hidden or dismissed window leaves false (§5.3)
        bool mapped = false;

        // whether a renderer presents through the surface, which then owns every buffer it commits (§6.3)
        bool renderer_presenting = false;

        // the surfaces a role rebuild released, which the renderer destroys once it released its wsi surface (§10.5)
        std::vector<wl_surface*> retired;
    };

    // the wayland objects a renderer surface attaches through, carried as one opaque handle (§3.5, AD-05)
    struct WaylandNativeSurface : linux_wsi::Surface
    {
        // the shared state the window clears before it destroys the surface (§10.5)
        std::shared_ptr<WaylandSurfaceState> state;
    };

    // destroy the surface a teardown handed over, which runs once the renderer released the state (§10.5)
    inline void destroy_surface_state(WaylandSurfaceState* state) {
        // the display still stands, because the owner member outlives this call (§10.5)
        if (state->display != nullptr) {
            if (state->pending_surface != nullptr)
                wl_surface_destroy(state->pending_surface);
            // a retired surface is released here too, because the renderer that held it is gone (§10.5)
            for (wl_surface* surface : state->retired)
                wl_surface_destroy(surface);
            wl_display_flush(state->display);
        }
        delete state;
    }
} // namespace albor::wayland
