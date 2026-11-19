#include <cstdlib>
#include <string_view>

#include "albor/platform/linux/wayland/clipboard.h"
#include "albor/platform/linux/wayland/event_loop.h"
#include "albor/platform/linux/wayland/file_dialog.h"

namespace
{
    // the installer that registers the wayland factories at load, so linking the adapter is the selection (§5.3, AD-16)
    struct PlatformAutoInstall
    {
        PlatformAutoInstall() {
            // the environment names the wayland session, which an x11 host leaves absent
            const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
            const char* session_type = std::getenv("XDG_SESSION_TYPE");
            const bool wayland_session = wayland_display != nullptr || (session_type != nullptr && std::string_view(session_type) == "wayland");
            // a host with no display keeps the wayland adapter, which is the only linux adapter that answered there
            const char* display = std::getenv("DISPLAY");
            if (!wayland_session && display != nullptr && *display != '\0')
                return;
            // the clipboard surface is installed first, because it names no shell object the loop needs (§3.5)
            albor::wayland::install_wayland_clipboard();
            // the file dialog follows, which the desktop portal answers where one runs (§5.3)
            albor::wayland::install_wayland_file_dialog();
            // the loop and window factories follow, which are what a run is built on (§5.3)
            albor::wayland::install_wayland_platform();
        }
    };

    // the installer is never referenced, because the load runs it before any factory is read (§5.3)
    const PlatformAutoInstall g_platform_auto_install;
} // namespace
