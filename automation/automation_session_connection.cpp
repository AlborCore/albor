#include "automation/automation_session_connection.h"

#include <utility>

#include "automation/automation_protocol.h"

namespace albor::automation
{
    SessionConnection::SessionConnection(AutomationRegistry& registry, std::string token) :
        m_session(registry, std::move(token)) {}

    std::optional<std::string> SessionConnection::handle(std::string_view line) {
        // the raw protocol always answers, because a handshake and a command both have a result (§10.3)
        return encode_result(m_session.handle(line));
    }
} // namespace albor::automation
