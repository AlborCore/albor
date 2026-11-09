#pragma once

#include "albor/ui/ui_node.h"

namespace albor
{
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

        // set the four edge insets from an Insets record (§8.2)
        void set_padding(const Insets& insets);

        // hand out the four edge insets (§8.2)
        [[nodiscard]] const Insets& padding() const;

    private:
        // the insets applied to every edge of the child (§8.2)
        Insets m_padding;
    };
} // namespace albor
