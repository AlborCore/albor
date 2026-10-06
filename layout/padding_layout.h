#pragma once

#include "ui/ui_node.h"

namespace albor
{
    // the four edge insets a padding container applies to the child it wraps, which is what separates content from an edge (§8.2)
    struct EdgeInsets
    {
        // the inset above the child in logical pixels (§8.2)
        float top = 0.0f;

        // the inset to the right of the child in logical pixels (§8.2)
        float right = 0.0f;

        // the inset below the child in logical pixels (§8.2)
        float bottom = 0.0f;

        // the inset to the left of the child in logical pixels (§8.2)
        float left = 0.0f;

        // report whether the insets consume no space at all, which is what an unconfigured padding carries (§8.2)
        [[nodiscard]] bool empty() const;

        // compare two inset sets field by field
        friend bool operator==(const EdgeInsets& lhs, const EdgeInsets& rhs) = default;
    };

    // the container that insets its single child, which is how content is kept away from an edge (§8.2)
    class PaddingLayout final : public UiNode
    {
    public:
        PaddingLayout() = default;

        // measure the child inside the space the insets leave, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // inset the assigned bounds and arrange the child inside, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;

        // set the same inset on all four edges, which a uniform padding change remeasures (§8.2)
        void set_padding(float uniform);

        // set a different inset on each edge, which a per edge padding change remeasures (§8.2)
        void set_padding(float top, float right, float bottom, float left);

        // hand out the four edge insets (§8.2)
        [[nodiscard]] const EdgeInsets& padding() const;

    private:
        // the insets applied to every edge of the child (§8.2)
        EdgeInsets m_padding;
    };
} // namespace albor
