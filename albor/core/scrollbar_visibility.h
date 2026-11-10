#pragma once

namespace albor
{
    // when a scrollbar is drawn, which is a per instance decision rather than a global one (§8)
    enum class ScrollbarVisibility
    {
        // the bar appears only while the content exceeds the viewport (§8)
        AsNeeded,

        // the content still scrolls, with no visible indicator at all (§8)
        AlwaysOff,

        // the bar is drawn whether or not the content overflows (§8)
        AlwaysOn
    };
} // namespace albor
