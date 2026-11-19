#pragma once

#include <xcb/xcb.h>

namespace albor::x11
{
    // dispatch one xcb event onto the window it names, which the loop calls on the ui thread (§4.1)
    void dispatch_x11_event(xcb_generic_event_t* event);
} // namespace albor::x11
