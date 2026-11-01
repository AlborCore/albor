# Grid

A container that places its children into equal-sized cells, in order — a table-shaped
layout whose cells share one row height and one column width.

## Properties

| property | type | notes |
| --- | --- | --- |
| `spacing` | `float` | the gap between cells, in both directions; density-scaled, accepts `px` |
| `columns` | `int` | the number of columns; `0` derives it from `rows` or the child count |
| `rows` | `int` | the number of rows; `0` derives it from `columns` or the child count |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Zero or more, flowing into cells in source order.

## Shape

A grid resolves its shape from the two counts, and a count of `0` means *derive*:

| written | result |
| --- | --- |
| `columns` and `rows` | the written pair |
| `columns` only | that many columns, as many rows as the children need |
| `rows` only | that many rows, as many columns as the children need |
| neither | one column of every child |

A derived count follows the children, so a `Repeat` or an `if` that adds a row changes the
shape without another assignment. The declaration order of `columns` and `rows` does not
matter; writing both is the same grid either way.

## Example

```text
Grid {
    id: matrix;
    spacing: 8px;
    columns: 2;

    Label { text: cell_a; horizontal_align: Align.center; }
    Label { text: cell_b; horizontal_align: Align.center; }
    Label { text: cell_c; horizontal_align: Align.center; }
    Label { text: cell_d; horizontal_align: Align.center; }
}
```

Each cell measures its child against the cell's share of the offered extent, and the grid
takes the largest child's measure — a short child does not shrink its cell.

## Spans

A child that covers several cells is a C++ concern today: `GridLayout::set_span(index,
GridSpan{columns, rows})` records it, and the arrange pass places the child across the cells
it claims. A span must fit the grid as it stands when the span is set, so declare `columns` and
`rows` first. The `.ui` language has no attached per-child placement property yet, so a form
that needs a spanning cell builds that grid through C++.

## C++ API

`GridLayout::set_columns(int)` and `GridLayout::set_rows(int)` take the same counts the
language writes, with `0` meaning *derive*; a negative count is refused. `set_columns(columns,
rows)` sets an explicit pair in one call.
