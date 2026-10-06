#include "layout/column_layout.h"

#include <algorithm>

namespace albor
{
    void ColumnLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // an empty column measures as nothing, because there is no child to take an extent from (§8.2)
        Size total{};
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        for (const auto& child : children()) {
            // each child is measured unconstrained, because the column learns its own extent from the result (§8.2)
            Size child_size{};
            child->measure(SizeConstraints::unconstrained(), child_size);
            // the heights accumulate, because the children stack vertically (§8.2)
            total.height += child_size.height;
            // the column is as wide as its widest child, because every child receives the full width (§8.2)
            total.width = std::max(total.width, child_size.width);
            // only the gaps between children count, so the first child adds none (§8.2)
            if (placed > 0)
                total.height += m_gap;
            // increment the count of placed children
            placed++;
        }
        // the total is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void ColumnLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the pen advances as children are placed, so the next child starts below the previous one (§8.2)
        float offset = bounds.y;
        for (const auto& child : children()) {
            // the first child sits at the top edge, so a gap only shifts the ones that follow it (§8.2)
            if (offset > bounds.y)
                offset += m_gap;
            // the child keeps the height the measure phase gave it, which is what makes the column honour its content (§8.2)
            const float child_height = child->measured_size().height;
            // the child receives the full available width, which is what aligns the column on one edge (§8.2)
            child->arrange(Rect::from_origin_size(Point{.x = bounds.x, .y = offset}, Size{.width = bounds.width, .height = child_height}));
            // the pen moves past the child just placed, so the next one starts after it (§8.2)
            offset += child_height;
        }
    }

    void ColumnLayout::set_gap(float gap) {
        // an unchanged gap remeasures nothing, because the layout did not change (§8.2)
        if (m_gap == gap)
            return;
        // the gap is stored first, so the invalidation walks a column that already knows its new spacing (§8.2)
        m_gap = gap;
        // a gap change moves the children, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    float ColumnLayout::gap() const {
        // the gap is the spacing a measure pass accounted for (§8.2)
        return m_gap;
    }
} // namespace albor
