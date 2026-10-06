// the wayland window of the prototype: the wl_frame pacing loop, libdecor decoration and xdg_positioner popups
// this is a scaffold: it declares the event contract, and the measurement is still pending

#include "prototypes/a2/platform.h"

#if !defined(__linux__)
#error "prototypes/a2/linux is only built on linux hosts"
#endif

namespace albor::a2::linux
{

    // the frame pacing contract of §10.3, recorded here so the slice stays reviewable
    struct FramePacing
    {
        // the refresh period the compositor reports through wl_output, in milliseconds
        int refresh_msec = 16;

        // whether a wl_frame callback is already requested, which is what throttles the loop to the refresh
        bool callback_pending = false;

        // whether a frame was requested while a callback was already pending, which is a coalesced request
        bool request_coalesced = false;

        // the time of the last requested frame callback, used to detect a missed refresh
        long long last_callback_msec = 0;
    };

    // the client side decoration contract of §8: libdecor owns the title bar, so resize and close are server driven
    struct Decoration
    {
        void* decor = nullptr; // libdecor_context
        void* decoration = nullptr; // libdecor_decoration
        bool attached = false;
        bool server_side = false;
    };

    // the popup contract of §22: a transient popup needs an xdg_positioner and a grab, which is the nested session
    struct Popup
    {
        void* positioner = nullptr; // xdg_positioner
        void* popup = nullptr; // xdg_popup
        void* surface = nullptr; // wl_surface
        bool grabbed = false;
    };

    // the listeners a2-l1 will install on the globals the compositor advertises
    void bind_registry(void* registry) {
        static_cast<void>(registry);

        // bind wl_compositor, xdg_wm_base and wl_shm, then stop: every other global is optional for this slice
    }

    // the configure handler a2-l2 will install, which must adopt the size before the frame is built (§9.1)
    void on_configure(int width, int height, int scale) {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);

        // remember the size the compositor asked for, so the next buffer carries it
    }

} // namespace albor::a2::linux
