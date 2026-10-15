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
| [Grid](../components/grid.md) | in cells | `spacing`; a table layout |
| [Padding](../components/padding.md) | one child, inset | `padding` |
| [ScrollView](../components/scroll-view.md) | one content child, scrollable | viewport, offset, scrollbar policy |

Every element also carries the three common properties:

```text
horizontal_align: Align.stretch;   // start | center | end | stretch
vertical_align: Align.center;
visible: armed;                    // hide in place, keep state ([control flow](../ui-language/control-flow.md#visible-hide-in-place))
```

The alignment axes are **screen axes** — horizontal is always horizontal — even inside a
`Column` whose main axis is vertical. `vertical_align` is never the parent's main axis.

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

The full source of these — `align_panel.ui` in `examples/form/` — is the reference for the
idioms a real form is built from.

## Skipping work

A change that alters a measured size must invalidate **layout**; a change that only alters
paint must invalidate **repaint**. The compiler generates the right one for every generated
binding, which is why a slider drag repaints its track rather than relayouting the tree. In
hand-written components that rule is the component's own to honour.
