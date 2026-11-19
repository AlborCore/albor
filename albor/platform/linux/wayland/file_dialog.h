#pragma once

namespace albor::wayland
{
    // install the portal file dialog backend, which the platform installer calls at load (§5.3, AD-16)
    void install_wayland_file_dialog();
} // namespace albor::wayland
