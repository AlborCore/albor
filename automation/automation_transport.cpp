#include "automation/automation_transport.h"

namespace albor::automation::platform
{
    // report whether this build compiled a local transport, which the portable side asks before building one (§10.3)
    [[nodiscard]] bool available();

    // build the local transport over a registry, which is where the socket type is finally named (§10.3, AD-19)
    [[nodiscard]] std::unique_ptr<AutomationTransport> build(AutomationRegistry& registry);
} // namespace albor::automation::platform

namespace albor::automation
{
    std::unique_ptr<AutomationTransport> create_transport(AutomationRegistry& registry) {
        // a build with no transport hands back null rather than something that pretends to listen (§10.3, AD-10)
        if (!platform::available())
            return nullptr;
        // the platform builds it, because only it can name the socket type (§10.3, AD-19)
        return platform::build(registry);
    }
} // namespace albor::automation
