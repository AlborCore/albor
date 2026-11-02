#pragma once

namespace albor::macos
{
    // install the macOS pasteboard clipboard, which the platform adapter calls once at load (§3.5, AD-16)
    void install_macos_clipboard();
} // namespace albor::macos
