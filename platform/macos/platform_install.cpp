#include "platform/macos/event_loop.h"

namespace
{
    // the installer that registers the appkit factories at load, so linking the adapter is the selection (§5.3, AD-16)
    struct PlatformAutoInstall
    {
        PlatformAutoInstall() { albor::macos::install_macos_platform(); }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§5.3)
    const PlatformAutoInstall g_platform_auto_install;
} // namespace

// the install mark every platform adapter defines, so two adapters on one line refuse the binary (§5.3, AD-16)
extern "C" int albor_platform_marker = 1;
