#include "automation/automation_platform.h"

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
