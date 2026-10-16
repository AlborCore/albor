# Grid

A container that places its children into equal-sized cells, in order — a table-shaped
layout whose cells share one row height and one column width.

## Properties

| property | type | notes |
| --- | --- | --- |
| `spacing` | `float` | the gap between cells, in both directions; density-scaled, accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Zero or more, flowing into cells in source order.

## Example

```text
Grid {
    id: matrix;
    spacing: 8px;

    Label { text: cell_a; horizontal_align: Align.center; }
    Label { text: cell_b; horizontal_align: Align.center; }
    Label { text: cell_c; horizontal_align: Align.center; }
    Label { text: cell_d; horizontal_align: Align.center; }
}
```

Each cell measures its child against the cell's share of the offered extent, and the grid
takes the largest child's measure — a short child does not shrink its cell.

## Cell count is a C++ concern today

A grid's shape — how many columns and rows it divides into — is set from C++ through
`GridLayout::set_columns(columns, rows)`; it is not yet exposed as a `.ui` property, and the
default is a single cell. From `.ui` alone a `Grid` therefore lays its children into one
cell, so a multi-cell grid from `.ui` waits on that property. The C++ path is stable and
covers spans (`GridSpan`) as well; the `chart` example reaches layout through C++ for the
same reason.
