# Layout

Layout answers one question per frame: where does each element sit, and how big is it. Albor
answers it with a two-phase contract, logical units that scale with the display, and a
container set small enough to reason about whole.

## The two phases

1. **Measure** — the parent queries each child for its minimum, preferred and maximum size
   given the constraints it offers. Text elements shape their intrinsic bounds through the
   text engine; a `Canvas` reports the intrinsic size its `width`/`height` declare.
2. **Arrange** — the parent assigns each child its final logical rectangle.

Both phases run **only when something can have changed**: each node carries a layout-dirty
flag and a render-dirty flag. A paint-only change never relayouts. A resize — a change of the
window's metrics — forces a layout pass regardless of the flags, so a size no handler
invalidated cannot leave a stale bound behind.

## Units

| written | meaning |
| --- | --- |
| `12px` | **logical pixels** — the unit layout works in; scale with display density |
| `12phx` | **physical device pixels** — always one device pixel; do not scale |

`px` is the default choice. Properties that are lengths — `spacing`, `padding`, `font_size`,
a `Canvas` extent, a viewport size — accept either; the density-scaled ones treat `px` as the
logical unit they were designed around. A 1-pixel hairline is what `phx` is for.

## Containers

| container | arranges its children | notes |
| --- | --- | --- |
| [Column](../components/column.md) | top to bottom | `spacing`, `padding` |
| [Row](../components/row.md) | left to right | `spacing` |
| [Stack](../components/stack.md) | on top of each other | overlays; no properties |
| [Grid](../components/grid.md) | in cells | `spacing`, `columns`, `rows`; a table layout |
| [Padding](../components/padding.md) | one child, inset | `padding` (uniform `px` or asymmetric `Insets`) |
| [ScrollView](../components/scroll-view.md) | one content child, scrollable | viewport, offset, scrollbar policy |
| [ListBox](../components/list-box.md) | a list of rows, scrollable | selection, row height, scrollbar policy |

Every element also carries the common properties:

```text
horizontal_align: Align.stretch;   // start | center | end | stretch
vertical_align: Align.center;
visible: armed;                    // hide in place, keep state ([control flow](../ui-language/control-flow.md#visible-hide-in-place))
```

The alignment axes are **screen axes**. In a `Row`, `horizontal_align` places a child on the
main axis and `vertical_align` on the cross axis; in a `Column` the two swap. On the main
axis, `Align.stretch` is also the declaration that a child is **flexible**: it absorbs spare
space, and when the fixed children overflow it is measured against the space they leave. On
the cross axis, `Align.stretch` fills the parent's content extent.

## Patterns

### The spacer idiom

A child with no natural size and `stretch` absorbs the leftover space on its axis:

```text
Row {
    spacing: 4px;

    Button { text: @tr("Run"); }                        // holds the leading edge
    Label { horizontal_align: Align.stretch; }          // the filler: absorbs the leftover
    Label { text: status_text; }                        // pushed to the far end
}
```

No width is declared anywhere: the middle child takes what the row does not need.

### A centred footer

```text
Column {
    spacing: 2px;
    vertical_align: Align.center;   // on the children, not the column
    // children...
}
```

### A parameter row where the value takes the rest

```text
Row {
    spacing: 8px;

    Label { text: @tr("Frequency"); }
    Label { horizontal_align: Align.stretch; text: frequency_text; }
}
```

### A scrolling page beside a fixed sidebar

A `ScrollView` with no declared viewport extent follows the space its parent offers. Inside a
`Row` or `Column`, declare the parent's main axis as `stretch` so the view is the child that
takes the remaining space:

```text
Column {
    padding: 16px;

    Row {
        vertical_align: Align.stretch;      // the body takes the height the actions leave

        Column {                            // a fixed width sidebar
            Label { text: @tr("Simulation"); }
        }

        ScrollView {
            horizontal_align: Align.stretch; // the page takes the width the sidebar leaves
            scrollbar: Scrollbar.on;

            Column { /* the page content */ }
        }
    }

    Row {
        horizontal_align: Align.end;
        Button { text: @tr("OK"); }
    }
}
```

Without `horizontal_align: Align.stretch` the scroll view is a fixed child: it measures the
whole offered width, so the row overflows instead of silently shrinking it.

### When the fixed children do not fit

A `Row` or `Column` first measures every visible child against its content extent. If the
total, the gaps and the padding fit, nothing else happens. If they overflow, only the
children that declared `Align.stretch` on the main axis are measured again, each with an
equal share of the space the fixed children did not use. A fixed child keeps exactly the
extent it measured, even when that means the row or column overflows.

Flexibility is therefore **declared, never inferred** from a child's measured size: a child
that happens to measure as wide as the offer is still fixed unless it says `stretch`.

The full source of these — `align_panel.ui` in `examples/form/` — is the reference for the
idioms a real form is built from.

## Skipping work

A change that alters a measured size must invalidate **layout**; a change that only alters
paint must invalidate **repaint**. The compiler generates the right one for every generated
binding, which is why a slider drag repaints its track rather than relayouting the tree. In
hand-written components that rule is the component's own to honour.
