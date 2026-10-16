# ScrollView

A container that gives its single content child a viewport the user can scroll — and, at
scale, a virtualized window over a large list.

## Properties

| property | type | notes |
| --- | --- | --- |
| `viewport_width` | `float` | the viewport's width; density-scaled, accepts `px` |
| `viewport_height` | `float` | the viewport's height; density-scaled, accepts `px` |
| `overscan` | `float` | extra extent rendered past each edge to hide populate cost; accepts `px` |
| `offset` | `float` | the current scroll position |
| `scrollbar` | `scrollbar` | `Scrollbar.as_needed`, `Scrollbar.off`, `Scrollbar.on` |
| `row_height` | `float` | the row pitch a virtualized `Repeat` lays out with; accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `scrolled` | `offset: float` | the scroll position changes |

## Children

Exactly one content child — the surface that scrolls. Put a [Column](column.md) or a
virtualized [Repeat](../ui-language/control-flow.md#virtualized-scrolling) inside it.

## Example

```text
component TaskList {
    in property<vector<Task>> tasks;

    ScrollView {
        id: task_scroller;
        viewport_height: 320px;
        row_height: 24px;
        scrollbar: Scrollbar.as_needed;

        // virtualized: only visible rows are constructed
        Repeat(task in tasks) {
            Label { text: task.title; }
        }

        on_scrolled: {
            // offset is available to handlers like any other signal parameter
        }
    }
}
```

## Virtualized scrolling

At scale a `Repeat` over a `ScrollView` virtualizes: it constructs rows for the visible
window plus the `overscan` band, keys them by the record's `key`
([Records](../concepts/native-components.md#records)), and recycles them as the offset moves.
That is what keeps a ten-thousand-row list at a constant node count.

The `key` field is not decoration: a background update that only changes one row's fields
reuses the existing element and rebinds it, rather than rebuilding the list.

## Notes

- `overscan` trades a band of pre-built rows against visible populate cost; it is measured
  in the same units as the viewport.
- `Scrollbar.on` forces the rail even when it is not needed; `Scrollbar.off` removes it —
  useful when a custom indicator is drawn instead.
- `row_height` is the pitch virtualization assumes; a row whose measured height differs
  still scrolls correctly, but the window estimate is the row height.
- A multi-line [TextField](text-field.md) scrolls its own viewport internally, so it does not
  need a `ScrollView` around it.
