#pragma once

#include <memory>

#include "automation/automation_registry.h"
#include "automation/automation_transport.h"

namespace albor::automation::platform
{
    // report whether this build compiled a local transport, which the portable side asks first (§10.3, AD-10)
    [[nodiscard]] bool available();

    // build the local transport over a registry, which is where the socket type is finally named (§10.3, AD-19)
    [[nodiscard]] std::unique_ptr<AutomationTransport> build(AutomationRegistry& registry);
} // namespace albor::automation::platform
