// the win32 window of the prototype: the message loop, the nested move/size session and the dpi contract
// this is a scaffold: it declares the entry points and the message contract, and the measurement is still pending

#include "prototypes/a2/platform.h"

#ifndef _WIN32
#error "prototypes/a2/win32 is only built on windows hosts"
#endif

#include <windows.h>

namespace albor::a2::win32
{

    // the message contract the platform layer of §8 relies on, recorded here so the slice stays reviewable
    enum FrameMessage : UINT
    {
        k_tick_message = WM_APP + 1, // the pacing tick, the equivalent of the macos display link
        k_present_message = WM_APP + 2, // a frame finished on the render thread, presented on the ui thread
    };

    // the window procedure that a2-w2 will measure: it must keep servicing input while wm_entersizemove owns the loop
    LRESULT CALLBACK frame_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_SIZE:
            // mark the live resize so the next presented frame can be attributed to it (§7.4)
            return 0;

        case WM_DPICHANGED:
            // adopt the suggested rectangle and update the authoritative metrics before the next frame (§9.1)
            return 0;

        case WM_ENTERSIZEMOVE:
            // the nested loop is the re-entrancy case that a2-w2 measures (§7.4)
            return 0;

        case WM_DISPLAYCHANGE:
            // a display topology change may force a swap chain recreation (§9.2)
            return 0;

        case k_tick_message:
        case k_present_message:
            // both are handled by the frame source, not by the window procedure
            return 0;

        default:
            // let the default handler deal with everything else
            break;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

} // namespace albor::a2::win32
