#include "layout/row_layout.h"

#include <algorithm>

namespace albor
{
    void RowLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // an empty row measures as nothing, because there is no child to take an extent from (§8.2)
        Size total{};
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        for (const auto& child : children()) {
            // each child is measured unconstrained, because the row learns its own extent from the result (§8.2)
            Size child_size{};
            child->measure(SizeConstraints::unconstrained(), child_size);
            // the widths accumulate, because the children sit side by side (§8.2)
            total.width += child_size.width;
            // the row is as tall as its tallest child, because every child receives the full height (§8.2)
            total.height = std::max(total.height, child_size.height);
            // only the gaps between children count, so the first child adds none (§8.2)
            if (placed > 0)
                total.width += m_gap;
            placed++;
        }
        // the total is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void RowLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the pen advances as children are placed, so the next child starts after the previous one (§8.2)
        float offset = bounds.x;
        for (const auto& child : children()) {
            // the first child sits at the left edge, so a gap only shifts the ones that follow it (§8.2)
            if (offset > bounds.x)
                offset += m_gap;
            // the child keeps the width the measure phase gave it, which is what makes the row honour its content (§8.2)
            const float child_width = child->measured_size().width;
            // the child receives the full available height, which is what aligns the row on one baseline (§8.2)
            child->arrange(Rect::from_origin_size(Point{.x = offset, .y = bounds.y}, Size{.width = child_width, .height = bounds.height}));
            // the pen moves past the child just placed, so the next one starts after it (§8.2)
            offset += child_width;
        }
    }

    void RowLayout::set_gap(float gap) {
        // an unchanged gap remeasures nothing, because the layout did not change (§8.2)
        if (m_gap == gap)
            return;
        // the gap is stored first, so the invalidation walks a row that already knows its new spacing (§8.2)
        m_gap = gap;
        // a gap change moves the children, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    float RowLayout::gap() const {
        // the gap is the spacing a measure pass accounted for (§8.2)
        return m_gap;
    }
} // namespace albor
