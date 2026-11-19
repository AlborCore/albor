#include <cstdlib>
#include <string_view>

#include "albor/platform/linux/x11/event_loop.h"

namespace
{
    // the installer that registers the xcb factories at load, so linking the adapter is the selection (§5.3, AD-16)
    struct PlatformAutoInstall
    {
        PlatformAutoInstall() {
            // the wayland session keeps its own adapter, which the umbrella links beside this one
            const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
            const char* session_type = std::getenv("XDG_SESSION_TYPE");
            const bool wayland_session = wayland_display != nullptr || (session_type != nullptr && std::string_view(session_type) == "wayland");
            const char* display = std::getenv("DISPLAY");
            // an x11 host is the one with a display and no wayland session, which the factories install on
            if (wayland_session || display == nullptr || *display == '\0')
                return;
            albor::x11::install_x11_platform();
        }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§5.3)
    const PlatformAutoInstall g_platform_auto_install;
} // namespace
