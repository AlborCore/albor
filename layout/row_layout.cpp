#include "layout/row_layout.h"

#include <algorithm>

namespace albor
{
    void RowLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // an empty row measures as nothing, because there is no child to take an extent from (§8.2)
        Size total{};
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        // the offered ceiling travels down, because a parent knows the space the child has to fit in (§8.2)
        const SizeConstraints child_constraints = SizeConstraints::at_most(constraints.maximum);
        for (const auto& child : children()) {
            // each child measures against that ceiling, so content never grows wider or taller than the row (§8.2)
            Size child_size{};
            child->measure(child_constraints, child_size);
            // the widths accumulate, because the children sit side by side (§8.2)
            total.width += child_size.width;
            // the row is as tall as its tallest child, because a child aligns vertically inside the row (§8.2)
            total.height = std::max(total.height, child_size.height);
            // only the gaps between children count, so the first child adds none (§8.2)
            if (placed > 0)
                total.width += m_gap;
            placed++;
        }
        // the padding sits outside the content, so it adds to both axes of what the children asked for (§8.2)
        total.width += m_padding * 2.0f;
        // the same padding applies on the other axis, because the inset is uniform (§8.2)
        total.height += m_padding * 2.0f;
        // the total is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void RowLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, while the measured size stands as the measure settled it (§8.2)
        m_bounds = bounds;
        // the children occupy the row less its padding, which is what the padding insets (§8.2)
        const Rect content = content_rect(bounds);
        // the width the children and the gaps ask for, which the leftover is the difference from (§8.2)
        const float natural = natural_width();
        // how many children take the leftover, which is what a spacer asks for (§8.2)
        std::size_t stretching = 0;
        for (const auto& child : children()) {
            // only a horizontally stretching child absorbs the leftover, because the others sit at a fixed offset (§8.2)
            if (resolve_align(child->horizontal_align(), Align::Start) == Align::Stretch)
                stretching++;
        }
        // the leftover is what the children did not ask for, and a negative one is an overflow (§8.2)
        const float leftover = content.width - natural;
        // each stretching child takes an equal share, so two spacers split the row between them (§8.2)
        const float share = stretching == 0 ? 0.0f : std::max(0.0f, leftover) / static_cast<float>(stretching);
        // the pen advances as children are placed, so the next child starts after the previous one (§8.2)
        float offset = content.x;
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        for (const auto& child : children()) {
            // the first child sits at the leading edge, so a gap only shifts the ones that follow it (§8.2)
            if (placed > 0)
                offset += m_gap;
            // the horizontal alignment is resolved against the default a row implies, which is the leading edge (§8.2)
            const Align horizontal = resolve_align(child->horizontal_align(), Align::Start);
            // the width the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_width = child->measured_size().width;
            // the origin is where the horizontal alignment puts this child inside the row (§8.2)
            float child_x = offset;
            // a centred child sits half the leftover in, which is what centring means on the main axis (§8.2)
            if (horizontal == Align::Center)
                child_x += leftover * 0.5f;
            // an end aligned child sits the whole leftover in, which pushes it to the trailing edge (§8.2)
            else if (horizontal == Align::End)
                child_x += leftover;
            // a stretching child takes its share on top of what it asked for, which is the spacer idiom (§8.2)
            const float width = horizontal == Align::Stretch ? child_width + share : child_width;
            // the vertical alignment is resolved against the default a row implies, which is the full height (§8.2)
            const Align vertical = resolve_align(child->vertical_align(), Align::Stretch);
            // the height the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_height = child->measured_size().height;
            // the origin is where the vertical alignment puts this child inside the row (§8.2)
            float child_y = content.y;
            // a vertically centred child sits half the leftover in, which is what centring means on the cross axis (§8.2)
            if (vertical == Align::Center)
                child_y += (content.height - child_height) * 0.5f;
            // a vertically end aligned child sits the whole leftover in, which pushes it to the bottom (§8.2)
            else if (vertical == Align::End)
                child_y += content.height - child_height;
            // the vertical extent is the whole row for a stretching child and what it asked for otherwise (§8.2)
            const float height = vertical == Align::Stretch ? content.height : child_height;
            // the child receives the rectangle its two alignments resolved to (§8.2)
            arrange_if_changed(*child, Rect::from_origin_size(Point{.x = child_x, .y = child_y}, Size{.width = width, .height = height}));
            // the pen moves past the child just placed, so the next one starts after it (§8.2)
            offset += width;
            placed++;
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

    void RowLayout::set_padding(float padding) {
        // an unchanged padding remeasures nothing, because the layout did not change (§8.2)
        if (m_padding == padding)
            return;
        // the padding is stored first, so the invalidation walks a row that already knows its new inset (§8.2)
        m_padding = padding;
        // a padding change moves the children, so the measure and arrange passes run again (§8.2)
        invalidate_layout();
    }

    float RowLayout::padding() const { return m_padding; }

    float RowLayout::natural_width() const {
        // the widths and the gaps are summed apart from the row, which is what the leftover is measured from (§8.2)
        float total = 0.0f;
        // a child before the first one has no gap before it, so the count tracks the gaps actually placed (§8.2)
        std::size_t placed = 0;
        for (const auto& child : children()) {
            // the measured width is what the measure phase settled for this child (§8.2)
            total += child->measured_size().width;
            // only the gaps between children count, so the first child adds none (§8.2)
            if (placed > 0)
                total += m_gap;
            placed++;
        }
        return total;
    }

    Rect RowLayout::content_rect(const Rect& bounds) const {
        // the leading inset is capped at the extent, so a wide padding still keeps children inside (§8.2)
        const float left = std::min(m_padding, bounds.width);
        // the same cap applies on the other axis, because the inset is uniform (§8.2)
        const float top = std::min(m_padding, bounds.height);
        // the trailing inset is capped at what the leading one left, which keeps the inner area non negative (§8.2)
        const float right = std::min(m_padding, std::max(0.0f, bounds.width - left));
        // the same cap applies to the trailing vertical inset, because the inset is uniform (§8.2)
        const float bottom = std::min(m_padding, std::max(0.0f, bounds.height - top));
        return Rect::from_origin_size(Point{.x = bounds.x + left, .y = bounds.y + top}, Size{.width = bounds.width - left - right, .height = bounds.height - top - bottom});
    }
} // namespace albor
