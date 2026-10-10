#pragma once

#include <functional>
#include <string>
#include <vector>

#include "automation/automation_registry.h"
#include "commands/command_registry.h"

namespace albor::automation
{
    // the live command registry an automation command reads and invokes, which the host owns (§10.3, AD-20)
    using CommandSnapshot = std::function<CommandRegistry*()>;

    // the commands that drive the shared actions, selecting only by author assigned id and never by a hint (§18.1, AD-11)
    void register_command_commands(AutomationRegistry& registry, CommandSnapshot snapshot);

    // the command names the command commands register, which a host allow-lists by name (§10.3, AD-10)
    [[nodiscard]] std::vector<std::string> command_command_names();
} // namespace albor::automation
