#pragma once

#include <string>

#include "automation/automation_connection.h"
#include "automation/automation_registry.h"
#include "automation/automation_session.h"

namespace albor::automation
{
    // default manual host connection, no adapter installed
    class SessionConnection final : public AutomationConnection
    {
    public:
        // session built over published token, client authenticates transport
        SessionConnection(AutomationRegistry& registry, std::string token);

        // answer a handshake or a command with the documented raw wire line, which is a reply every time (§10.3)
        [[nodiscard]] std::optional<std::string> handle(std::string_view line) override;

    private:
        // the conversation this client owns, which gates every command on its handshake (§10.3, AD-10)
        AutomationSession m_session;
    };
} // namespace albor::automation
