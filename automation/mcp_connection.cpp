#include "automation/mcp_connection.h"

#include <utility>

namespace albor::automation
{
    McpConnection::McpConnection(AutomationRegistry& registry, std::string token) :
        m_session(registry, std::move(token)) {
        // the route is built here because it captures this connection and cannot outlive it (§10.3, AD-20)
        McpRoute route;
        // the tool list is the layer's own allow list, which is the only source of what may be called (§10.3, AD-20)
        route.permitted_commands = [&registry] { return registry.allowed_commands(); };
        // every tool call goes through the session, which is the automation layer's own gate (§10.3, AD-20)
        route.run = [this](std::string_view line) { return m_session.handle(line); };
        // the initialize token is compared by the session, which never discloses the expected value (§10.3, AD-10)
        route.authenticate = [this](std::string_view token) { return m_session.authenticate(token).authenticated; };
        // every later request reads the same session, so a second connection can never inherit this proof (§10.3, AD-10)
        route.authenticated = [this] { return m_session.authenticated(); };
        // the adapter is built over the route, which is the whole of its reach (§10.3, AD-20)
        m_server = std::make_unique<McpServer>(std::move(route));
    }

    std::optional<std::string> McpConnection::handle(std::string_view line) {
        // the adapter answers a request and stays silent for a notification, which is the protocol's shape (§10.3, §12.2)
        return m_server->handle_line(line);
    }
} // namespace albor::automation
