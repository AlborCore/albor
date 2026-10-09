#pragma once

#include <memory>

#include "accessibility/accessibility_adapter.h"

namespace albor::accessibility::platform
{
    // report whether this build compiled a platform bridge, which the portable side asks before building one (§10.2)
    [[nodiscard]] bool available();

    // build the platform bridge, which is where the platform type is finally named (§10.2, AD-05)
    [[nodiscard]] std::unique_ptr<AccessibilityAdapter> build();
} // namespace albor::accessibility::platform
