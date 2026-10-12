#include "automation/automation_platform.h"

namespace albor::automation::platform
{
    bool available() {
        // the named pipe transport is not written yet, so this build reports none rather than a stub (§10.3, AD-16)
        return false;
    }

    std::unique_ptr<AutomationTransport> build(AutomationRegistry&) {
        // an unavailable platform never builds one, because the portable side checks first (§10.3, AD-10)
        return nullptr;
    }
} // namespace albor::automation::platform
