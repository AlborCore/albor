#pragma once

#include <cstddef>
#include <vector>

#include "ui/ui_node.h"

namespace albor
{
    // how many cells a child occupies in one direction of a grid, where one is a single cell (§8.2)
    struct GridSpan
    {
        // how many columns the child occupies, where one is a single cell (§8.2)
        std::size_t columns = 1;

        // how many rows the child occupies, where one is a single cell (§8.2)
        std::size_t rows = 1;

        // report whether the span covers at least one cell, because a span of zero would place nothing (§8.2)
        [[nodiscard]] bool single_cell() const;

        // compare two spans field by field
        friend bool operator==(const GridSpan& lhs, const GridSpan& rhs) = default;
    };

    // the container that places its children in equal cells, which is the multi cell layout of §8.2
    class GridLayout final : public UiNode
    {
    public:
        GridLayout() = default;

        // measure every child inside its cell and take the largest, which is the measure phase of §8.2
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // place every child in its cell, which is the arrange phase of §8.2
        void arrange(const Rect& bounds) override;

        // set the number of columns and rows, which a change remeasures because the cells move (§8.2)
        void set_columns(std::size_t columns, std::size_t rows);

        // hand out the number of columns the grid divides into (§8.2)
        [[nodiscard]] std::size_t columns() const;

        // hand out the number of rows the grid divides into (§8.2)
        [[nodiscard]] std::size_t rows() const;

        // set the span of the child at an index, which is what makes a child occupy several cells (§8.2)
        bool set_span(std::size_t index, GridSpan span);

        // hand out the span of the child at an index, which is a single cell when none was set (§8.2)
        [[nodiscard]] GridSpan span_at(std::size_t index) const;

        // set the space between two cells, which a change remeasures because the cells shrink (§8.2)
        void set_gap(float gap);

        // hand out the space between two cells (§8.2)
        [[nodiscard]] float gap() const;

    private:
        // the size of the cell at an index, which is the assigned extent divided by the count (§8.2)
        [[nodiscard]] Size cell_size(const Rect& bounds) const;

        // the position of a cell at a column and row, which is the origin the gaps move (§8.2)
        [[nodiscard]] Point cell_origin(const Rect& bounds, std::size_t column, std::size_t row) const;

        // the span of the child at an index, which clamps the index because arrange walks every child (§8.2)
        [[nodiscard]] GridSpan effective_span(std::size_t index) const;

        // the spans of the children, which are parallel to the children themselves (§8.2)
        std::vector<GridSpan> m_spans;

        // how many columns the grid divides into (§8.2)
        std::size_t m_columns = 1;

        // how many rows the grid divides into (§8.2)
        std::size_t m_rows = 1;

        // the space between two cells in logical pixels (§8.2)
        float m_gap = 0.0f;
    };
} // namespace albor
