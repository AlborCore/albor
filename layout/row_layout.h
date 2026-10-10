#pragma once

#include "ui/ui_node.h"

namespace albor
{
    // the container that places its children left to right with a configurable gap, which is a linear layout (§8.2)
    class RowLayout final : public UiNode
    {
    public:
        RowLayout() = default;

        // sum the measured widths and the gaps, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // place every child at its aligned position on both axes, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;

        // set the distance between two consecutive children, which a gap change remeasures (§8.2)
        void set_gap(float gap);

        // hand out the distance between two consecutive children (§8.2)
        [[nodiscard]] float gap() const;

        // set the space inside the row on every side, which a padding change remeasures (§8.2)
        void set_padding(float padding);

        // hand out the space inside the row on every side (§8.2)
        [[nodiscard]] float padding() const;

    private:
        // the width the children and the gaps ask for, which the leftover is measured against (§8.2)
        [[nodiscard]] float natural_width() const;

        // the rectangle the children occupy inside the assigned bounds, which the padding insets (§8.2)
        [[nodiscard]] Rect content_rect(const Rect& bounds) const;

        // the distance between two consecutive children in logical pixels (§8.2)
        float m_gap = 0.0f;

        // the space inside the row on every side in logical pixels (§8.2)
        float m_padding = 0.0f;
    };
} // namespace albor
