#pragma once

namespace albor::windows
{
    // install the win32 file dialog backend, which the platform installer calls at load (§5.3, AD-16)
    void install_windows_file_dialog();
} // namespace albor::windows
