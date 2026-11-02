#pragma once

namespace albor::windows
{
    // install the windows clipboard surface, which the platform adapter calls once at load (§3.5, AD-16)
    void install_windows_clipboard();
} // namespace albor::windows
