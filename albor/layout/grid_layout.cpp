#include "albor/layout/grid_layout.h"

#include <algorithm>

namespace albor
{
    bool GridSpan::single_cell() const {
        // a span of no cells would place nothing, so it is not a single cell either (§8.2)
        return columns == 1 && rows == 1;
    }

    void GridLayout::measure(const SizeConstraints& constraints, Size& out_size) {
        // the effective shape is resolved once, because a derived count depends on the children (§8.2)
        const std::size_t column_count = columns();
        const std::size_t row_count = rows();
        // the gaps consume part of the offered extent, so a cell measures against its share of what is left (§8.2)
        const float available_width = std::max(0.0f, constraints.maximum.width - m_gap * static_cast<float>(column_count - 1));
        // the same subtraction happens on the other axis, because a cell is as constrained as it is wide (§8.2)
        const float available_height = std::max(0.0f, constraints.maximum.height - m_gap * static_cast<float>(row_count - 1));
        // the extent divides by the count, and the effective count is never zero so the divisor is always usable (§8.2)
        const Size cell{.width = available_width / static_cast<float>(column_count), .height = available_height / static_cast<float>(row_count)};
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
        const Size total{.width = largest.width * static_cast<float>(column_count) + m_gap * static_cast<float>(column_count - 1), .height = largest.height * static_cast<float>(row_count) + m_gap * static_cast<float>(row_count - 1)};
        // the result is clamped into the offered constraints, because a parent decision is final (§8.2)
        m_measured_size = constraints.constrain(total);
        out_size = m_measured_size;
    }

    void GridLayout::arrange(const Rect& bounds) {
        // the assigned bounds replace the previous ones, while the measured size stands as the measure settled it (§8.2)
        m_bounds = bounds;
        // the effective shape decides the cells, because a derived count follows the children (§8.2)
        const std::size_t column_count = columns();
        const std::size_t row_count = rows();
        // the cell is the assigned extent divided by the count, which is what every child is placed in (§8.2)
        const Size cell = cell_size(bounds);
        // the cursor is a linear cell index, because a span advances by whole cells on a grid read in rows (§8.2)
        std::size_t cursor = 0;
        for (std::size_t index = 0; index < child_count(); index++) {
            // the cursor names the column and row from the grid width, which is what makes it wrap (§8.2)
            const std::size_t column = cursor % column_count;
            const std::size_t row = cursor / column_count;
            // the span is read per child, because each one may cover a different number of cells (§8.2)
            const GridSpan span = effective_span(index);
            // a span past the last cell would place a child outside the grid, so it is capped (§8.2)
            const std::size_t span_columns = std::min(span.columns, column_count - column);
            // the same cap applies on the other axis, and the cursor never names a row past the last (§8.2)
            const std::size_t span_rows = std::min(span.rows, row_count - row);
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
            arrange_if_changed(child, Rect::from_origin_size(Point{.x = child_x, .y = child_y}, Size{.width = width, .height = height}));
            // the cursor advances by every cell the span covered, which is what keeps the next child clear of them (§8.2)
            cursor += span_columns + (span_rows - 1) * column_count;
            // a cursor past the last cell wraps to the first, because a fixed grid has no more room (§8.2)
            if (cursor >= column_count * row_count)
                cursor = 0;
        }
    }

    void GridLayout::set_columns(std::size_t columns, std::size_t rows) {
        // a grid with no cells cannot place a child, so a count of zero is refused rather than accepted (§8.2)
        if (columns == 0 || rows == 0)
            return;
        // an unchanged explicit count moves nothing, so the layout passes are left alone (§8.2)
        if (!m_columns_auto && !m_rows_auto && m_columns == columns && m_rows == rows)
            return;
        // the count is stored first, so the invalidation walks a grid that already divides into it (§8.2)
        m_columns = columns;
        m_rows = rows;
        // an explicit pair is what the caller asked for, so neither axis is derived (§8.2)
        m_columns_auto = false;
        m_rows_auto = false;
        // the cells move, so the measure and arrange passes must run again (§8.2)
        invalidate_layout();
    }

    void GridLayout::set_columns(int columns) {
        // a negative count is a request the language cannot produce, refused rather than clamped (§8.2)
        if (columns < 0)
            return;
        // zero asks the grid to derive the count from the rows or the children (§8.2)
        const bool derive = columns == 0;
        // a derived axis keeps one as an unused placeholder, because the effective count answers instead (§8.2)
        const std::size_t count = derive ? 1 : static_cast<std::size_t>(columns);
        // an unchanged request moves nothing, so the layout passes are left alone (§8.2)
        if (m_columns_auto == derive && (derive || m_columns == count))
            return;
        // the request is stored first, so the invalidation walks a grid that already knows its shape (§8.2)
        m_columns_auto = derive;
        m_columns = count;
        // the cells move, so the measure and arrange passes must run again (§8.2)
        invalidate_layout();
    }

    void GridLayout::set_rows(int rows) {
        // a negative count is a request the language cannot produce, refused rather than clamped (§8.2)
        if (rows < 0)
            return;
        // zero asks the grid to derive the count from the columns or the children (§8.2)
        const bool derive = rows == 0;
        // a derived axis keeps one as an unused placeholder, because the effective count answers instead (§8.2)
        const std::size_t count = derive ? 1 : static_cast<std::size_t>(rows);
        // an unchanged request moves nothing, so the layout passes are left alone (§8.2)
        if (m_rows_auto == derive && (derive || m_rows == count))
            return;
        // the request is stored first, so the invalidation walks a grid that already knows its shape (§8.2)
        m_rows_auto = derive;
        m_rows = count;
        // the cells move, so the measure and arrange passes must run again (§8.2)
        invalidate_layout();
    }

    std::size_t GridLayout::columns() const {
        // the effective count is what the last measure and arrange pass divided by (§8.2)
        return effective_columns();
    }

    std::size_t GridLayout::rows() const {
        // the effective count is what the last measure and arrange pass divided by (§8.2)
        return effective_rows();
    }

    std::size_t GridLayout::effective_columns() const {
        // the claimed area is what a derived axis divides, so a span is not counted as one cell (§8.2)
        const std::size_t count = claimed_cells();
        // an explicit column count is what the author wrote, and it stands as it is (§8.2)
        if (!m_columns_auto)
            return m_columns;
        // an explicit row count divides the claimed area into that many rows (§8.2)
        if (!m_rows_auto)
            return (count + m_rows - 1) / m_rows;
        // neither was written, so the grid falls back to one column of every child (§8.2)
        return 1;
    }

    std::size_t GridLayout::effective_rows() const {
        // the claimed area is what a derived axis divides, so a span is not counted as one cell (§8.2)
        const std::size_t count = claimed_cells();
        // an explicit row count is what the author wrote, and it stands as it is (§8.2)
        if (!m_rows_auto)
            return m_rows;
        // an explicit column count divides the claimed area into that many columns (§8.2)
        if (!m_columns_auto)
            return (count + m_columns - 1) / m_columns;
        // neither was written, so every claimed cell takes one row of the single column (§8.2)
        return count;
    }

    std::size_t GridLayout::claimed_cells() const {
        // every child claims the area of its span, which is what a derived axis must count (§8.2)
        std::size_t cells = 0;
        // the children are walked in order, because a span is recorded by index (§8.2)
        for (std::size_t index = 0; index < child_count(); index++) {
            // the effective span is the recorded one or the single cell a plain child occupies (§8.2)
            const GridSpan span = effective_span(index);
            // the area is the product, which is the number of cells the cursor advances by (§8.2)
            cells += span.columns * span.rows;
        }
        // an empty grid still asks a valid question, so the count is never zero (§8.2)
        return std::max<std::size_t>(cells, 1);
    }

    bool GridLayout::set_span(std::size_t index, GridSpan span) {
        // an index past the end names no child, so there is nothing to give a span (§8.2)
        if (index >= child_count())
            return false;
        // a span of no cells would place nothing, so it is refused (§8.2)
        if (span.columns == 0 || span.rows == 0)
            return false;
        // a span wider than the grid cannot be honoured, so it is refused rather than clamped (§8.2)
        if (span.columns > columns() || span.rows > rows())
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
        // the effective shape decides how many cells share the extent (§8.2)
        const std::size_t column_count = columns();
        const std::size_t row_count = rows();
        // the gaps consume part of the extent, so the cells divide what is left (§8.2)
        const float available_width = std::max(0.0f, bounds.width - m_gap * static_cast<float>(column_count - 1));
        // the same subtraction happens on the other axis, because a cell is square in count only (§8.2)
        const float available_height = std::max(0.0f, bounds.height - m_gap * static_cast<float>(row_count - 1));
        // the extent divides by the count, and the effective count is never zero so the divisor is always usable (§8.2)
        return Size{.width = available_width / static_cast<float>(column_count), .height = available_height / static_cast<float>(row_count)};
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
