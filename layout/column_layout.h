#pragma once

#include "ui/ui_node.h"

namespace albor
{
    // the container that places its children top to bottom with a configurable gap, which is a linear layout (§8.2)
    class ColumnLayout final : public UiNode
    {
    public:
        ColumnLayout() = default;

        // sum the measured heights and the gaps, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // place every child at its measured height and the full width, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;

        // set the distance between two consecutive children, which a gap change remeasures (§8.2)
        void set_gap(float gap);

        // hand out the distance between two consecutive children (§8.2)
        [[nodiscard]] float gap() const;

        // set the inset that frames the children on every edge, which a padding change remeasures (§8.2)
        void set_padding(float padding);

        // hand out the inset that frames the children (§8.2)
        [[nodiscard]] float padding() const;

    private:
        // the distance between two consecutive children in logical pixels (§8.2)
        float m_gap = 0.0f;

        // the inset the children sit inside, in logical pixels (§8.2)
        float m_padding = 0.0f;
    };
} // namespace albor
