#pragma once

namespace albor::macos
{
    // install the appkit file dialog backend, which the platform installer calls at load (§5.3, AD-16)
    void install_macos_file_dialog();
} // namespace albor::macos
