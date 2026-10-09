#pragma once

#include <memory>
#include <string>

#include "automation/automation_registry.h"

namespace albor::automation
{
    // where the server listens and where it publishes its token, which are both local paths (§10.3, AD-10)
    struct AutomationEndpoint
    {
        // the local socket the server binds, which is a filesystem path rather than a network address (§10.3, AD-10)
        std::string socket_path;

        // the file the per session token is written to, which the client reads to authenticate (§10.3, AD-10)
        std::string token_path;
    };

    // the transport that carries automation commands between processes, which is local only (§10.3, AD-10)
    class AutomationTransport
    {
    public:
        virtual ~AutomationTransport() = default;

        // bind the endpoint and start listening, which fails rather than degrading to an open port (§10.3, AD-10)
        [[nodiscard]] virtual bool listen(const AutomationEndpoint& endpoint) = 0;

        // hand out the token this server publishes, which the host writes wherever its client will read it (§10.3, AD-10)
        [[nodiscard]] virtual const std::string& session_token() const = 0;

        // serve one connection to completion, which returns false once the listener is stopped (§10.3)
        [[nodiscard]] virtual bool serve_one() = 0;

        // stop listening, which an application calls when it shuts down (§4.1)
        virtual void stop() = 0;

        // report the last refusal, which is what a host logs when a client is turned away (§10.3, AD-10)
        [[nodiscard]] virtual const std::string& last_error() const = 0;
    };

    // the transport this build provides over a registry, which is null where the platform has none (§10.3, AD-10)
    [[nodiscard]] std::unique_ptr<AutomationTransport> create_transport(AutomationRegistry& registry);
} // namespace albor::automation
