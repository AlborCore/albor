#pragma once

#include <functional>
#include <string>

#include "automation/automation_registry.h"
#include "semantics/semantic_provider.h"

namespace albor::automation
{
    // the live semantic tree an automation command reads and writes, which the host owns (§10.3, AD-20)
    using SemanticSnapshot = std::function<SemanticTree*()>;

    // the commands that drive the application, selecting only by author assigned id, never by a hint (§18.1, AD-11)
    void register_semantic_commands(AutomationRegistry& registry, SemanticSnapshot snapshot);

    // the command names the semantic commands register, which a host allow-lists by name (§10.3, AD-10)
    [[nodiscard]] std::vector<std::string> semantic_command_names();
} // namespace albor::automation
