#include "platform/windows/event_loop.h"

namespace
{
    // the installer that registers the win32 factories at load, so linking the adapter is the selection (§5.3, AD-16)
    struct PlatformAutoInstall
    {
        PlatformAutoInstall() {
            // the loop and window factories are registered, which are what a run is built on (§5.3)
            albor::windows::install_windows_platform();
        }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§5.3)
    const PlatformAutoInstall g_platform_auto_install;
} // namespace

// the install mark every platform adapter defines, so two adapters on one line refuse the binary (§5.3, AD-16)
extern "C" int albor_platform_marker = 3;
