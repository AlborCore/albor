#pragma once

namespace albor
{
    // the node that wants to know when the focus moves onto or off it, which a caret is (§8.3)
    class FocusTarget
    {
    public:
        virtual ~FocusTarget() = default;

        // take or drop the focus, which the focus manager calls on the node it moved to or from (§8.3)
        virtual void set_focused(bool focused) = 0;
    };
} // namespace albor