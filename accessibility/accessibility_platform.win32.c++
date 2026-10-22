#include "accessibility/accessibility_platform.h"

namespace albor::accessibility::platform
{
    bool available() {
        // the ui automation bridge is not written yet, so this build reports none rather than a stub (§10.2, AD-16)
        return false;
    }

    std::unique_ptr<AccessibilityAdapter> build() {
        // an unavailable platform never builds one, because the portable side checks first (§10.2)
        return nullptr;
    }
} // namespace albor::accessibility::platform
