#pragma once

#include "ui/ui_node.h"

namespace albor
{
    // the container that layers every child over the same origin, which is how an overlay is built (§8.2)
    class StackLayout final : public UiNode
    {
    public:
        StackLayout() = default;

        // measure the largest child against the offered constraints, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // give every child the whole of the assigned bounds, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;
    };
} // namespace albor
