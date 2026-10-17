# ListBox

A container that hosts a virtualized list of rows under one selection, navigable by
keyboard.

## Properties

| property | type | notes |
| --- | --- | --- |
| `selected` | `int` | the selected row index, where `selected: -1` clears the selection |
| `row_height` | `float` | the row pitch a virtualized `Repeat` lays out with; accepts `px` |
| `viewport_width` | `float` | the list's width; density-scaled, accepts `px` |
| `viewport_height` | `float` | the list's height; density-scaled, accepts `px` |
| `overscan` | `float` | extra rows built past each edge to hide populate cost; accepts `px` |
| `scrollbar` | `scrollbar` | `Scrollbar.as_needed`, `Scrollbar.off`, `Scrollbar.on` |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `selection_changed` | — | another row becomes selected, or the selection is cleared |
| `activated` | `key: string` | the focused row is activated with Return or Space, or by an assistive action |

## Children

A virtualized [Repeat](../ui-language/control-flow.md#virtualized-scrolling) over a
[record](../concepts/native-components.md#records) — one repeat body per row, keyed by the
record's `key` field.

## Example

```text
component Inbox {
    inout property<vector<Task>> items;
    signal row_opened(string key);

    ListBox {
        id: inbox_list;
        viewport_height: 320px;
        row_height: 28px;
        overscan: 40px;
        scrollbar: Scrollbar.as_needed;
        selected: 0;

        // virtualized: only visible rows are constructed
        Repeat(task in items) {
            Label { text: task.title; }
        }

        on_selection_changed: {
        }

        on_activated: {
            row_opened(key);
        }
    }
}
```

## Keyboard navigation

The list is one focusable control; its rows forward their keys to it. With focus anywhere
inside the list:

- `ArrowUp` / `ArrowDown` move the selection one row in either direction, clamped at the ends.
- `Home` / `End` jump to the first and last row.
- `PageUp` / `PageDown` move one viewport's worth of rows.
- `Return` / `Space` activate the focused row and raise `activated`.

Each move keeps the new row visible by scrolling the internal viewport. The list owns these
keys, so the internal viewport never scrolls instead of moving the selection. A modified key
is left to the application, and a disabled list claims no key at all.

## Pointer selection

A press on a row selects it and moves the focus onto it, which is what pointing at a row
means. A press on the empty space below the last row names no row, so the list leaves it to a
parent. Assistive technology drives the same navigation through the list's semantic actions —
`select_next`, `select_previous`, `select_first`, `select_last` and `activate` — which mirror
the keys exactly.

## Selection

A keyboard move carries the selection with it, but focus and selection are stored and
reported separately — a row may be focused without being selected. The selected row is drawn
with a highlight band, so a selection is visible and not only announced. The `activated`
signal passes the identity `key` of the row that was activated (see
[Records](../concepts/native-components.md#records)), so a handler acts on the record itself:

```text
on_activated: {
    row_opened(key);
}
```

A selection declared before the rows exist — `selected: 2;` set before the `Repeat` builds
its first row — is applied once the rows materialize, so a default selection in a component
property works.

## Virtualized scrolling

A `Repeat` written directly inside a `ListBox` behaves like one written inside a
[ScrollView](scroll-view.md): it builds only the rows the visible window plus the `overscan`
band needs, keys them by the record's `key` field, and recycles what leaves the window. The
list's `row_height` is the pitch virtualization divides the viewport by.

## Notes

- `selected` names a row by its position in the collection the `Repeat` walks, in item order.
- Each row announces itself as a list item carrying its own `selected` and `focused` state,
  which is what screen readers and automation read.
- A `ListBox` is a viewport over its rows, so a free-scrolling surface without selection is a
  [ScrollView](scroll-view.md); the list does not expose a writable `offset` of its own.
