#include "layout/grid_layout.h"

#include <algorithm>

namespace albor
{
    bool GridSpan::single_cell() const {
        // a span of no cells would place nothing, so it is not a single cell either (§8.2)
        return columns == 1 && rows == 1;
    }

    void GridLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // the gaps consume part of the offered extent, so a cell measures against its share of what is left (§8.2)
        const float available_width = std::max(0.0f, constraints.maximum.width - m_gap * static_cast<float>(m_columns - 1));
        // the same subtraction happens on the other axis, because a cell is as constrained as it is wide (§8.2)
        const float available_height = std::max(0.0f, constraints.maximum.height - m_gap * static_cast<float>(m_rows - 1));
        // the extent divides by the count, and set_columns refused a zero count so the divisor is always usable (§8.2)
        const Size cell{.width = available_width / static_cast<float>(m_columns), .height = available_height / static_cast<float>(m_rows)};
        // a cell measures its child against the share, which is what lets a span cover several cells (§8.2)
        SizeConstraints inner = SizeConstraints::at_most(cell);
        // the grid is as large as the largest cell its children asked for (§8.2)
        Size largest{};
        // the children are measured in order, because a span moves the ones that follow it (§8.2)
        for (std::size_t index = 0; index < child_count(); index++) {
            // each child is measured unconstrained first, because the grid learns its cell from the result (§8.2)
            Size child_size{};
            child_at(index)->measure(inner, child_size);
            // the largest child decides the cell, because every cell is the same size (§8.2)
            largest.width = std::max(largest.width, child_size.width);
            largest.height = std::max(largest.height, child_size.height);
        }
        // the gaps are added back, because the grid is the cells plus the space between them (§8.2)
        const Size total{.width = largest.width * static_cast<float>(m_columns) + m_gap * static_cast<float>(m_columns - 1), .height = largest.height * static_cast<float>(m_rows) + m_gap * static_cast<float>(m_rows - 1)};
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void GridLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones (§8.2)
        m_bounds = bounds;
        // the measured size follows the assigned one, so a later measure starts from the truth (§8.2)
        m_measured_size = Size{.width = bounds.width, .height = bounds.height};
        // the cell is the assigned extent divided by the count, which is what every child is placed in (§8.2)
        const Size cell = cell_size(bounds);
        // the cursor is a linear cell index, because a span advances by whole cells on a grid read in rows (§8.2)
        std::size_t cursor = 0;
        for (std::size_t index = 0; index < child_count(); index++) {
            // the cursor names the column and row from the grid width, which is what makes it wrap (§8.2)
            const std::size_t column = cursor % m_columns;
            const std::size_t row = cursor / m_columns;
            // the span is read per child, because each one may cover a different number of cells (§8.2)
            const GridSpan span = effective_span(index);
            // a span past the last cell would place a child outside the grid, so it is capped (§8.2)
            const std::size_t span_columns = std::min(span.columns, m_columns - column);
            // the same cap applies on the other axis, and the cursor never names a row past the last (§8.2)
            const std::size_t span_rows = std::min(span.rows, m_rows - row);
            // the origin comes from the cell the child starts in, which the gaps move (§8.2)
            const Point origin = cell_origin(bounds, column, row);
            // the extent covers every cell the span claims, which is what makes a wide child wide (§8.2)
            const Size extent{.width = cell.width * static_cast<float>(span_columns) + m_gap * static_cast<float>(span_columns - 1), .height = cell.height * static_cast<float>(span_rows) + m_gap * static_cast<float>(span_rows - 1)};
            // the child is read for alignment, because every cell honours both axes independently (§8.2)
            UiNode& child = *child_at(index);
            // the horizontal alignment is resolved against the default a cell implies, which is the whole cell (§8.2)
            const Align horizontal = resolve_align(child.horizontal_align(), Align::Stretch);
            // the vertical alignment is resolved against the default a cell implies, which is the whole cell (§8.2)
            const Align vertical = resolve_align(child.vertical_align(), Align::Stretch);
            // the width the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_width = child.measured_size().width;
            // the origin is where the horizontal alignment puts the child inside the region its span claims (§8.2)
            float child_x = origin.x;
            // a centred child sits half the leftover in, which is what centring means inside a cell (§8.2)
            if (horizontal == Align::Center)
                child_x += (extent.width - child_width) * 0.5f;
            // an end aligned child sits the whole leftover in, which pushes it to the right of its region (§8.2)
            else if (horizontal == Align::End)
                child_x += extent.width - child_width;
            // the height the child asked for, which every alignment except stretching keeps (§8.2)
            const float child_height = child.measured_size().height;
            // the origin is where the vertical alignment puts the child inside the region its span claims (§8.2)
            float child_y = origin.y;
            // a vertically centred child sits half the leftover in, which is what centring means inside a cell (§8.2)
            if (vertical == Align::Center)
                child_y += (extent.height - child_height) * 0.5f;
            // a vertically end aligned child sits the whole leftover in, pushing it to the bottom (§8.2)
            else if (vertical == Align::End)
                child_y += extent.height - child_height;
            // the child receives the rectangle its two alignments resolved to inside the region (§8.2)
            const float width = horizontal == Align::Stretch ? extent.width : child_width;
            // the same rule applies on the other axis, so a cell honours both independently (§8.2)
            const float height = vertical == Align::Stretch ? extent.height : child_height;
            child.arrange(Rect::from_origin_size(Point{.x = child_x, .y = child_y}, Size{.width = width, .height = height}));
            // the cursor advances by every cell the span covered, which is what keeps the next child clear of them (§8.2)
            cursor += span_columns + (span_rows - 1) * m_columns;
            // a cursor past the last cell wraps to the first, because a fixed grid has no more room (§8.2)
            if (cursor >= m_columns * m_rows)
                cursor = 0;
        }
    }

    void GridLayout::set_columns(std::size_t columns, std::size_t rows) {
        // a grid with no cells cannot place a child, so a count of zero is refused rather than accepted (§8.2)
        if (columns == 0 || rows == 0)
            return;
        // an unchanged count moves nothing, so the layout passes are left alone (§8.2)
        if (m_columns == columns && m_rows == rows)
            return;
        // the count is stored first, so the invalidation walks a grid that already divides into it (§8.2)
        m_columns = columns;
        m_rows = rows;
        // the cells move, so the measure and arrange passes must run again (§8.2)
        invalidate_layout();
    }

    std::size_t GridLayout::columns() const {
        // the count is what the last measure pass divided by (§8.2)
        return m_columns;
    }

    std::size_t GridLayout::rows() const {
        // the count is what the last measure pass divided by (§8.2)
        return m_rows;
    }

    bool GridLayout::set_span(std::size_t index, GridSpan span) {
        // an index past the end names no child, so there is nothing to give a span (§8.2)
        if (index >= child_count())
            return false;
        // a span of no cells would place nothing, so it is refused (§8.2)
        if (span.columns == 0 || span.rows == 0)
            return false;
        // a span wider than the grid cannot be honoured, so it is refused rather than clamped (§8.2)
        if (span.columns > m_columns || span.rows > m_rows)
            return false;
        // the spans are grown to the child count, because a span is parallel to the children (§8.2)
        m_spans.resize(child_count(), GridSpan{});
        // the span is recorded, so the next arrange places the child over the cells it claims (§8.2)
        m_spans[index] = span;
        // a span moves the children that follow it, so the layout passes must run again (§8.2)
        invalidate_layout();
        return true;
    }

    GridSpan GridLayout::span_at(std::size_t index) const {
        // an index past the end names no child, so the default span answers (§8.2)
        if (index >= m_spans.size())
            return GridSpan{};
        // the recorded span is what the child was given (§8.2)
        return m_spans[index];
    }

    void GridLayout::set_gap(float gap) {
        // a negative gap would overlap the cells, which is a layout the author did not ask for (§8.2)
        if (gap < 0.0f)
            return;
        // an unchanged gap moves nothing, so the layout passes are left alone (§8.2)
        if (m_gap == gap)
            return;
        // the gap is stored first, so the invalidation walks a grid that already knows its spacing (§8.2)
        m_gap = gap;
        // the cells shrink, so the measure and arrange passes must run again (§8.2)
        invalidate_layout();
    }

    float GridLayout::gap() const {
        // the gap is the spacing the last measure pass accounted for (§8.2)
        return m_gap;
    }

    Size GridLayout::cell_size(const Rect& bounds) const {
        // the gaps consume part of the extent, so the cells divide what is left (§8.2)
        const float available_width = std::max(0.0f, bounds.width - m_gap * static_cast<float>(m_columns - 1));
        // the same subtraction happens on the other axis, because a cell is square in count only (§8.2)
        const float available_height = std::max(0.0f, bounds.height - m_gap * static_cast<float>(m_rows - 1));
        // the extent divides by the count, and set_columns refused a zero count so the divisor is always usable (§8.2)
        return Size{.width = available_width / static_cast<float>(m_columns), .height = available_height / static_cast<float>(m_rows)};
    }

    Point GridLayout::cell_origin(const Rect& bounds, std::size_t column, std::size_t row) const {
        // the cell size is the step between two cells, which is what the origin is built from (§8.2)
        const Size cell = cell_size(bounds);
        // the origin starts at the top left and steps by the cell plus the gap (§8.2)
        return Point{.x = bounds.x + (cell.width + m_gap) * static_cast<float>(column), .y = bounds.y + (cell.height + m_gap) * static_cast<float>(row)};
    }

    GridSpan GridLayout::effective_span(std::size_t index) const {
        // a child with no recorded span occupies one cell, which is what a plain grid gives it (§8.2)
        if (index >= m_spans.size())
            return GridSpan{};
        return m_spans[index];
    }
} // namespace albor
