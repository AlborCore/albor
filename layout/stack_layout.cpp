#include "layout/stack_layout.h"

#include <algorithm>

namespace albor
{
    void StackLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // an empty stack measures as nothing, because there is no child to take an extent from (§8.2)
        Size largest{};
        for (const auto& child : children()) {
            // each child is measured unconstrained, because the stack only learns the extent from the result (§8.2)
            Size child_size{};
            child->measure(SizeConstraints::unconstrained(), child_size);
            // the stack is as large as its largest child, because the children overlap and must all fit (§8.2)
            largest.width = std::max(largest.width, child_size.width);
            largest.height = std::max(largest.height, child_size.height);
        }
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(largest);
        out_size = m_measured_size;
    }

    void StackLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        for (const auto& child : children()) {
            // every child receives the whole of the stack, which is what makes them overlap (§8.2)
            child->arrange(bounds);
        }
    }
} // namespace albor
