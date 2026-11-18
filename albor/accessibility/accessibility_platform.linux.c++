#include "albor/accessibility/accessibility_platform.h"

namespace albor::accessibility::platform
{
    bool available() {
        // the linux adapter has no at-spi bridge yet, so the portable side reports none (§10.2, #42)
        return false;
    }

    std::unique_ptr<AccessibilityAdapter> build() {
        // a build with no bridge hands back null rather than a stub, so a caller can tell (§10.2)
        return nullptr;
    }
} // namespace albor::accessibility::platform
