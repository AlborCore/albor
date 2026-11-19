#include "albor/application/file_dialog.h"

#include "albor/platform/linux/wayland/file_dialog.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>

#include "albor/platform/linux/wayland/platform_window.h"
#include "albor/platform/linux/wayland/portal.h"
#include "albor/platform/linux/wayland/wayland_connection.linux.h"

namespace albor::wayland
{
    namespace
    {
        // the portal backend, which runs both directions through the desktop's own chooser (§5.3, AD-16)
        class PortalFileDialog final : public PlatformFileDialog
        {
        public:
            std::optional<std::filesystem::path> open(const OpenFileOptions& options, NativeWindowHandle parent) override {
                // the request is what the portal runs as a panel, which carries the open direction (§5.3)
                PortalFileRequest request;
                // the parent is the window identifier the portal stacks the panel over, where empty means unowned (§5.1)
                request.parent = portal_parent_handle(parent);
                // an empty title keeps the portal's own label (§5.3)
                request.title = options.title;
                // the filters are what the panel offers, in the author's order (§5.3)
                request.filters = options.filters;
                // an empty directory keeps the portal's last location (§5.3)
                request.initial_directory = options.initial_directory;
                // an open panel proposes no name, because the user picks the file (§5.3)
                request.save = false;
                return run(request);
            }

            std::optional<std::filesystem::path> save(const SaveFileOptions& options, NativeWindowHandle parent) override {
                // the request is what the portal runs as a panel, which carries the save direction (§5.3)
                PortalFileRequest request;
                // the parent is the window identifier the portal stacks the panel over, where empty means unowned (§5.1)
                request.parent = portal_parent_handle(parent);
                // an empty title keeps the portal's own label (§5.3)
                request.title = options.title;
                // the filters are what the panel offers, in the author's order (§5.3)
                request.filters = options.filters;
                // an empty directory keeps the portal's last location (§5.3)
                request.initial_directory = options.initial_directory;
                // the proposed name is what the panel starts from, which the user may replace (§5.3)
                request.default_name = options.default_name;
                // the direction decides which portal method runs the panel (§5.3)
                request.save = true;
                return run(request);
            }

        private:
            // run one panel through the portal, which reports a cancel as an empty answer (§5.3)
            [[nodiscard]] static std::optional<std::filesystem::path> run(const PortalFileRequest& request) {
                // the portal is one client per session bus, which every window shares (§5.3)
                const std::shared_ptr<PortalClient> portal = PortalClient::shared();
                // a host without a portal refuses rather than crashing, which the caller reads as a cancel (§5.3)
                if (!portal->available()) {
                    // the reason is printed, because the caller reads nullopt as a cancel (§5.3)
                    std::fprintf(stderr, "albor: the file dialog needs an XDG desktop portal\n");
                    return std::nullopt;
                }
                // the compositor connection is what the panel keeps answering while the portal runs (§5.3)
                const std::shared_ptr<WaylandConnection> connection = WaylandConnection::connect(std::string_view());
                // a host without a compositor has no connection to pump, which the portal call tolerates (§5.3)
                const std::function<void()> pump = [connection] {
                    // a connection that was never made has nothing to read (§5.3)
                    if (connection == nullptr)
                        return;
                    // the events the compositor already sent are dispatched, which answers its ping (§5.3)
                    connection->dispatch_pending();
                    // a readable display carries a compositor event, which is read without waiting (§5.3)
                    pollfd display = {connection->fd(), POLLIN, 0};
                    // a display that has nothing to say leaves the panel to the portal (§5.3)
                    if (poll(&display, 1, 0) > 0 && (display.revents & POLLIN) != 0)
                        connection->dispatch();
                    // the requests this client queued are sent, which keeps the compositor served (§5.3)
                    connection->flush();
                };
                // the panel runs until the user answers it or the portal goes away (§5.3)
                return portal->choose_file(request, pump);
            }
        };
    } // namespace

    void install_wayland_file_dialog() {
        // the factory builds the portal backend, which the application calls like any other (§5.3, AD-16)
        set_platform_file_dialog_factory([] { return std::unique_ptr<PlatformFileDialog>(new PortalFileDialog()); });
    }
} // namespace albor::wayland
