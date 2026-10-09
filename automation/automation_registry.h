#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "automation/automation_protocol.h"

namespace albor::automation
{
    // the work one command performs, which the registry holds behind its name (§10.3, AD-10)
    using CommandHandler = std::function<AutomationResult(const AutomationRequest&)>;

    // the commands a session may run and the ones it may not, which is the allow list (§10.3, AD-10)
    class AutomationRegistry
    {
    public:
        // register a command under a name, which replaces any command already registered under it (§10.3)
        void register_command(std::string name, CommandHandler handler);

        // report whether a command is registered at all, which a refusal names as the reason (§10.3)
        [[nodiscard]] bool knows(std::string_view name) const;

        // permit a command by name, which is what the allow list is (§10.3, AD-10)
        void allow(std::string name);

        // report whether a command is permitted, because a command that is not allowed never runs (§10.3, AD-10)
        [[nodiscard]] bool allows(std::string_view name) const;

        // hand out the permitted commands in order, which a client reads to learn what it may ask for (§10.3, AD-10)
        [[nodiscard]] std::vector<std::string> allowed_commands() const;

        // run a request, which refuses anything not permitted before the handler is reached (§10.3, AD-10)
        [[nodiscard]] AutomationResult dispatch(const AutomationRequest& request) const;

    private:
        // the handlers by name, which is what a dispatch looks the request up in (§10.3)
        std::map<std::string, CommandHandler, std::less<>> m_handlers;

        // the permitted names, kept apart from the handlers because permission belongs to the session (§10.3, AD-10)
        std::map<std::string, bool, std::less<>> m_allowed;
    };
} // namespace albor::automation
