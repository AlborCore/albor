// the wayland platform state of the prototype, so that no wayland type reaches prototypes/a2/platform.h (§3.5, AD-05)

#include "prototypes/a2/platform.h"

#if !defined(__linux__)
#error "prototypes/a2/linux is only built on linux hosts"
#endif

namespace albor::a2::linux
{

    // the opaque wayland surface the prototype presents into, the counterpart of the metal layer
    struct WaylandSurface
    {
        void* display = nullptr; // wl_display
        void* registry = nullptr; // wl_registry
        void* compositor = nullptr; // wl_compositor
        void* shell = nullptr; // xdg_wm_base
        void* surface = nullptr; // wl_surface
        void* frame = nullptr; // wl_frame
        void* buffer = nullptr; // wl_buffer
        void* shm_pool = nullptr; // wl_shm_pool
        unsigned int width = 0;
        unsigned int height = 0;
        int scale = 1;
        int refresh_msec = 16;
        bool configured = false;
    };

    // the opaque native surface, mirroring native_surface.macos.h so the render thread reaches the same contract
    struct NativeSurface
    {
        WaylandSurface* surface = nullptr;
    };

    // the counters the wl_frame loop must fill in, mirroring the macos state
    struct FrameCounters
    {
        unsigned long long frames_presented = 0;
        unsigned long long frames_presented_while_resizing = 0;
        unsigned long long configure_events = 0;
        unsigned long long scale_changes = 0;
        unsigned long long events_during_nested_loop = 0;
        unsigned long long events_total = 0;
        unsigned long long close_after_frames = 0;
        unsigned long long nested_loops_running = 0;
        unsigned long long coalesced_requests = 0;
        int resizing = 0;
    };

} // namespace albor::a2::linux

namespace albor::a2
{

    // the opaque platform state, so that the interface stays free of wayland types (§3.5, AD-16)
    struct PlatformWindowState
    {
        linux::WaylandSurface* surface = nullptr;
        linux::NativeSurface native{};
        linux::FrameCounters counters{};
        void* frame_callback = nullptr;
    };

} // namespace albor::a2
