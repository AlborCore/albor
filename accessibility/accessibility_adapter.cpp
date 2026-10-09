#include "accessibility/accessibility_adapter.h"

#include "accessibility/accessibility_platform.h"

namespace albor::accessibility
{
    bool AccessibilityAdapter::available() {
        // the platform source answers this, because only it knows which bridge this build compiled (§10.2)
        return platform::available();
    }

    std::unique_ptr<AccessibilityAdapter> create_adapter() {
        // a build with no bridge hands back null rather than a stub, so a caller can tell (§10.2)
        if (!platform::available())
            return nullptr;
        // the platform source builds the bridge, because only it can name the platform type (§10.2, AD-05)
        return platform::build();
    }
} // namespace albor::accessibility
