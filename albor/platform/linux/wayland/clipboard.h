#pragma once

namespace albor::wayland
{
    // install the wayland data device clipboard, which the platform adapter calls once at load (§3.5, AD-16)
    void install_wayland_clipboard();
} // namespace albor::wayland
