#pragma once

#include <string>
#include <string_view>

#include "automation/automation_registry.h"

namespace albor::automation
{
    // one authenticated conversation with a client, which owns its own per session token (§10.3, AD-10)
    class AutomationSession
    {
    public:
        // build a session over a registry, minting the token this conversation must present (§10.3, AD-10)
        explicit AutomationSession(AutomationRegistry& registry);

        // report whether a presented token is this session's, without ever disclosing the expected one (§10.3, AD-10)
        [[nodiscard]] HandshakeResult authenticate(std::string_view presented);

        // report whether the conversation has authenticated, which every later command requires (§10.3, AD-10)
        [[nodiscard]] bool authenticated() const;

        // run one wire line, which a handshake or a command arrives as (§10.3)
        [[nodiscard]] AutomationResult handle(std::string_view line);

        // the token this session issued, which the host writes to a file the client reads (§10.3, AD-10)
        [[nodiscard]] const std::string& token() const;

    private:
        // the commands this session runs, which the allow list governs (§10.3, AD-10)
        AutomationRegistry& m_registry;

        // the token minted for this conversation, which is compared and never logged (§10.3, AD-10)
        std::string m_token;

        // whether the conversation has proved the token, which gates every command (§10.3, AD-10)
        bool m_authenticated = false;
    };
} // namespace albor::automation
