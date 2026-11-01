#pragma once

#include <functional>
#include <string>
#include <vector>

#include "automation/automation_registry.h"

namespace albor
{
    class UiNode;
}

namespace albor::automation
{
    // the live tree a typing command reaches, which the host owns (§10.3)
    using InputRoot = std::function<UiNode*()>;

    // the commands that type into the focused control, selecting only by author assigned id (§18.1, AD-11)
    void register_input_commands(AutomationRegistry& registry, InputRoot root);

    // the command names the input commands register, which a host allow-lists by name (§10.3, AD-10)
    [[nodiscard]] std::vector<std::string> input_command_names();
} // namespace albor::automation
