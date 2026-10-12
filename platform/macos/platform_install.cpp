#include "platform/macos/clipboard.h"
#include "platform/macos/event_loop.h"

namespace
{
    // the installer that registers the appkit factories at load, so linking the adapter is the selection (§5.3, AD-16)
    struct PlatformAutoInstall
    {
        PlatformAutoInstall() {
            // the pasteboard surface is installed first, because it names no appkit object the loop needs (§3.5)
            albor::macos::install_macos_clipboard();
            // the loop and window factories follow, which are what a run is built on (§5.3)
            albor::macos::install_macos_platform();
        }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§5.3)
    const PlatformAutoInstall g_platform_auto_install;
} // namespace

// the install mark every platform adapter defines, so two adapters on one line refuse the binary (§5.3, AD-16)
extern "C" int albor_platform_marker = 1;
