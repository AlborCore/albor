#include "automation/automation_registry.h"

#include <utility>

namespace albor::automation
{
    void AutomationRegistry::register_command(std::string name, CommandHandler handler) {
        // registering does not permit, because a shipped command is not automatically reachable (§10.3, AD-10)
        m_handlers.insert_or_assign(std::move(name), std::move(handler));
    }

    bool AutomationRegistry::knows(std::string_view name) const {
        // a registered command is one the process could run, permitted or not (§10.3)
        return m_handlers.find(name) != m_handlers.end();
    }

    void AutomationRegistry::allow(std::string name) {
        // the name is recorded, which is what makes the command reachable for a session (§10.3, AD-10)
        m_allowed.insert_or_assign(std::move(name), true);
    }

    bool AutomationRegistry::allows(std::string_view name) const {
        // a name the list does not hold is not permitted, which is the default every session starts from (§10.3, AD-10)
        const auto found = m_allowed.find(name);
        // only an explicit permission counts, because silence is not consent (§10.3, AD-10)
        return found != m_allowed.end() && found->second;
    }

    std::vector<std::string> AutomationRegistry::allowed_commands() const {
        // the names are collected in the map's order, which makes the reply stable for a test (§10.3, AD-10)
        std::vector<std::string> names;
        // every permitted name is collected, which is what a client may ask for (§10.3)
        for (const auto& [name, permitted] : m_allowed) {
            // a permission that was withdrawn contributes nothing (§10.3, AD-10)
            if (permitted)
                names.push_back(name);
        }
        return names;
    }

    AutomationResult AutomationRegistry::dispatch(const AutomationRequest& request) const {
        // permission is checked first, so a refused command never reaches application code (§10.3, AD-10)
        if (!knows(request.command))
            return AutomationResult::refused("no such command: " + request.command);
        // the allow list is checked before the handler is found, so the refusal never discloses what exists (§10.3, AD-10)
        if (!allows(request.command))
            return AutomationResult::refused("command not allowed: " + request.command);
        // the handler is looked up only now, which is the first point a command could do anything (§10.3, AD-10)
        const auto found = m_handlers.find(request.command);
        // an allowed name with no handler is a registration defect, which is refused rather than crashed on (§10.3)
        if (found == m_handlers.end())
            return AutomationResult::refused("command has no handler: " + request.command);
        // the handler runs, which is the only path into the application (§10.3, AD-20)
        return found->second(request);
    }
} // namespace albor::automation
