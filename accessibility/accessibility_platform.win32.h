#pragma once

#include "accessibility/accessibility_adapter.h"

// the fragment root is forward declared, because the accessor names the platform interface only (§10.2)
struct IRawElementProviderFragmentRoot;

namespace albor::accessibility::platform
{
    // hand out the fragment root the win32 bridge publishes, which the platform suite drives directly (§10.2)
    [[nodiscard]] IRawElementProviderFragmentRoot* fragment_root_for(AccessibilityAdapter& adapter);
} // namespace albor::accessibility::platform
