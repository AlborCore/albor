#pragma once

#include <memory>
#include <string>

#include "automation/automation_connection.h"
#include "automation/automation_registry.h"
#include "automation/automation_session.h"
#include "automation/mcp_server.h"

namespace albor::automation
{
    // MCP connection through automation layer (§10.3, AD-20)
    class McpConnection final : public AutomationConnection
    {
    public:
        // the session owns the token comparison, and the server owns the protocol over it (§10.3, AD-10, AD-20)
        McpConnection(AutomationRegistry& registry, std::string token);

        // handle one MCP request line, or nothing when a notification takes no reply (§10.3, §12.2)
        [[nodiscard]] std::optional<std::string> handle(std::string_view line) override;

    private:
        // the conversation the client authenticates, which every tool call still traverses (AD-20)
        AutomationSession m_session;

        // the protocol adapter, which is built over a route that captures this connection's session (§10.3)
        std::unique_ptr<McpServer> m_server;
    };
} // namespace albor::automation
