#pragma once

namespace albor::linux_wsi
{
    // the window system a linux surface names, which only the linux adapters and the vulkan backend read
    enum class Kind
    {
        // the surface names a wayland display and wl_surface
        Wayland,

        // the surface names an xcb connection and window
        X11,
    };

    // the common head every linux adapter's surface bundle carries, so a renderer reads the window system (§3.5, AD-05)
    struct Surface
    {
        // the window system the concrete bundle behind this head belongs to
        Kind kind = Kind::Wayland;
    };
} // namespace albor::linux_wsi
