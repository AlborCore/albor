#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "albor/application/file_dialog.h"
#include "albor/core/color_scheme.h"

namespace albor::wayland
{
    // the opaque portal state, so that no dbus type reaches this header (§3.5, AD-05)
    struct PortalState;

    // one file panel the portal runs, which carries the request the framework described (§5.3)
    struct PortalFileRequest
    {
        // the window the panel is owned by, as the portal's window identifier, where empty means unowned (§5.1)
        std::string parent;

        // the panel title, which the portal shows
        std::string title;

        // the filters the panel offers, in the order the author wrote them
        std::vector<FileFilter> filters;

        // the directory the panel opens at, where empty means the portal's last location
        std::string initial_directory;

        // the name a save panel proposes, which the user may replace
        std::string default_name;

        // whether the panel saves rather than opens
        bool save = false;
    };

    // the registration an appearance observer holds, which releases it when the holder dies (§8.4)
    class PortalSubscription
    {
    public:
        // an empty registration observes nothing, which an unavailable portal leaves
        PortalSubscription() = default;

        // release the registration, which the portal stops notifying
        ~PortalSubscription();

        PortalSubscription(PortalSubscription&& other) noexcept;
        PortalSubscription& operator=(PortalSubscription&& other) noexcept;
        PortalSubscription(const PortalSubscription&) = delete;
        PortalSubscription& operator=(const PortalSubscription&) = delete;

    private:
        // the client owns the registration, which the subscription releases through it (§8.4)
        friend class PortalClient;

        // register the observer the holder installed, which the client names by identifier
        PortalSubscription(std::shared_ptr<PortalState> state, std::uint64_t id);

        // the state the registration lives in, which a dead client leaves behind (§8.4)
        std::weak_ptr<PortalState> m_state;

        // the identifier the observer was installed under, where zero names no registration
        std::uint64_t m_id = 0;
    };

    // the session bus client the desktop portals answer, which a host without one reports unavailable (§5.3, AD-16)
    class PortalClient : public std::enable_shared_from_this<PortalClient>
    {
    public:
        // hand out the process client for the session bus, which the same bus address always resolves to (§5.3)
        [[nodiscard]] static std::shared_ptr<PortalClient> shared();

        // release the bus connection and every registration
        ~PortalClient();

        PortalClient(const PortalClient&) = delete;
        PortalClient& operator=(const PortalClient&) = delete;
        PortalClient(PortalClient&&) = delete;
        PortalClient& operator=(PortalClient&&) = delete;

        // report whether the session bus answered, which a host without one reports (§5.3)
        [[nodiscard]] bool available() const;

        // run one file panel on the ui thread, where nullopt means the user cancelled or the portal refused (§5.3)
        [[nodiscard]] std::optional<std::filesystem::path> choose_file(const PortalFileRequest& request, const std::function<void()>& pump);

        // read the colour scheme the desktop advertises, which the portable dark scheme stands in for (§8.4)
        [[nodiscard]] ColorScheme color_scheme() const;

        // observe the colour scheme, which a window mirrors into its appearance (§8.4)
        [[nodiscard]] PortalSubscription subscribe_appearance(std::function<void(ColorScheme)> observer);

        // hand out the descriptor the loop polls, where a negative value names an unavailable portal (§4.1)
        [[nodiscard]] int fd() const;

        // dispatch the portal messages that already arrived, which a setting change arrives through (§8.4)
        void dispatch();

        // send the requests this client queued, which the portal answers (§5.3)
        void flush();

    private:
        // connect to the session bus and read the scheme, which a missing portal leaves unavailable
        PortalClient();

        // the portal state, which outlives every subscription through the shared pointer
        std::shared_ptr<PortalState> m_state;
    };
} // namespace albor::wayland
