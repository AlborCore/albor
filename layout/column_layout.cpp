#include "layout/column_layout.h"

#include <algorithm>

namespace albor
{
    void ColumnLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // an empty column measures as nothing, because there is no child to take an extent from (§8.2)
        Size total{};
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        // the offered ceiling shrinks by the insets, because the padding frames the children on every edge (§8.2)
        const SizeConstraints child_constraints = SizeConstraints::at_most(Size{.width = std::max(0.0f, constraints.maximum.width - m_padding * 2.0f), .height = std::max(0.0f, constraints.maximum.height - m_padding * 2.0f)});
        for (const auto& child : children()) {
            // each child measures against that ceiling, so text wraps inside the width the column leaves (§8.2, §9.1)
            Size child_size{};
            child->measure(child_constraints, child_size);
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
        // the inset adds on both sides, because the padding frames the children on every edge (§8.2)
        total.width += m_padding * 2.0f;
        total.height += m_padding * 2.0f;
        // the total is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void ColumnLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the leading inset is capped at the extent, so a padding wider than the column still keeps the children inside (§8.2)
        const float left = std::min(m_padding, bounds.width);
        const float top = std::min(m_padding, bounds.height);
        // the trailing inset is capped at what the leading one left, which keeps the inner area non negative (§8.2)
        const float right = std::min(m_padding, std::max(0.0f, bounds.width - left));
        const float bottom = std::min(m_padding, std::max(0.0f, bounds.height - top));
        // the inner area is where the children stack, which is what the padding insets (§8.2)
        const Rect inner = Rect::from_origin_size(Point{.x = bounds.x + left, .y = bounds.y + top}, Size{.width = bounds.width - left - right, .height = bounds.height - top - bottom});
        // the height the children and the gaps ask for, which the leftover is measured against (§8.2)
        float natural = 0.0f;
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t counted = 0;
        for (const auto& child : children()) {
            // the measured height is what the measure phase settled for this child (§8.2)
            natural += child->measured_size().height;
            // only the gaps between children count, so the first child adds none (§8.2)
            if (counted > 0)
                natural += m_gap;
            counted++;
        }
        // how many children take the leftover, which is what a spacer asks for (§8.2)
        std::size_t stretching = 0;
        for (const auto& child : children()) {
            // only a vertically stretching child absorbs the leftover, because the others sit at a fixed offset (§8.2)
            if (resolve_align(child->vertical_align(), Align::Stretch) == Align::Stretch)
                stretching++;
        }
        // the leftover is what the children did not ask for, and a negative one is an overflow (§8.2)
        const float leftover = inner.height - natural;
        // each stretching child takes an equal share, so two spacers split the column between them (§8.2)
        const float share = stretching == 0 ? 0.0f : std::max(0.0f, leftover) / static_cast<float>(stretching);
        // the pen advances as children are placed, so the next child starts below the previous one (§8.2)
        float offset = inner.y;
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        for (const auto& child : children()) {
            // the first child sits at the top edge, so a gap only shifts the ones that follow it (§8.2)
            if (placed > 0)
                offset += m_gap;
            // the vertical alignment is resolved against the default a column implies, which keeps the old layout (§8.2)
            const Align vertical = resolve_align(child->vertical_align(), Align::Start);
            // the height the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_height = child->measured_size().height;
            // the origin is where the vertical alignment puts this child inside the column (§8.2)
            float child_y = offset;
            // a centred child sits half the leftover in, which is what centring means on the main axis (§8.2)
            if (vertical == Align::Center)
                child_y += leftover * 0.5f;
            // an end aligned child sits the whole leftover in, which pushes it to the bottom (§8.2)
            else if (vertical == Align::End)
                child_y += leftover;
            // a stretching child takes its share on top of what it asked for, which is the spacer idiom (§8.2)
            const float height = vertical == Align::Stretch ? child_height + share : child_height;
            // the horizontal alignment is resolved against the default a column implies, which is the full width (§8.2)
            const Align horizontal = resolve_align(child->horizontal_align(), Align::Stretch);
            // the width the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_width = child->measured_size().width;
            // the origin is where the horizontal alignment puts this child inside the column (§8.2)
            float child_x = inner.x;
            // a horizontally centred child sits half the leftover in, which is centring on the cross axis (§8.2)
            if (horizontal == Align::Center)
                child_x += (inner.width - child_width) * 0.5f;
            // a horizontally end aligned child sits the whole leftover in, which pushes it to the right (§8.2)
            else if (horizontal == Align::End)
                child_x += inner.width - child_width;
            // the horizontal extent is the whole column for a stretching child and what it asked for otherwise (§8.2)
            const float width = horizontal == Align::Stretch ? inner.width : child_width;
            // the child receives the rectangle its two alignments resolved to (§8.2)
            child->arrange(Rect::from_origin_size(Point{.x = child_x, .y = child_y}, Size{.width = width, .height = height}));
            // the pen moves past the child just placed, so the next one starts after it (§8.2)
            offset += height;
            placed++;
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

    void ColumnLayout::set_padding(float padding) {
        // an unchanged padding remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == padding)
            return;
        // the inset is stored first, so the invalidation walks a column that already knows its new frame (§8.2)
        m_padding = padding;
        // a padding change moves the children, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    float ColumnLayout::padding() const {
        // the padding is the inset a measure pass accounted for (§8.2)
        return m_padding;
    }
} // namespace albor
