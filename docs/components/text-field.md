# TextField

A leaf that edits a string the user types — a form field, a search box, a value editor.
Single-line by default; set `multiline` for a wrapping, scrolling editor.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `string` | the current text |
| `placeholder` | `string` | the hint shown while empty |
| `read_only` | `bool` | displays but refuses editing |
| `multiline` | `bool` | accepts line breaks, wraps, and scrolls its own viewport |
| `disabled` | `bool` | refuses all interaction |
| `font_size` | `float` | density-scaled; accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `changed` | `value: string` | the text changes — per keystroke |
| `committed` | `value: string` | the edit is committed — Enter, or focus leaving |

## Children

None — a field is a leaf.

## Example

```text
component NameForm {
    inout property<string> name: "";
    inout property<int> commits: 0;

    Column {
        spacing: 8px;

        Label { text: @tr("Who is this form for?"); }

        TextField {
            id: name_field;
            value: name;                  // binds the field to the property
            placeholder: @tr("type a name");
            on_changed: {
                name = value;             // per-keystroke: value is the signal's parameter
            }
            on_committed: {
                commits = commits + 1;    // commit is a distinct event
            }
        }

        Label { text: @tr("typed ") + string(name); }
    }
}
```

## Multi-line fields

`multiline: true` turns the field into a wrapping, scrolling editor. The value wraps at the
width the parent offers, and the field measures to its wrapped run — so it grows as lines are
added, until the parent's constraints cap its height
([Layout](../concepts/layout.md#the-two-phases)). Past that cap the field scrolls its own
vertical viewport instead of letting the text clip:

```text
TextField {
    id: notes;
    multiline: true;
    value: notes_text;
    on_changed: { notes_text = value; }   // the same two-way pattern as the single-line case
}
```

The caret drives the scroll: moving, typing or pasting the caret onto a line outside the box
brings it back into view, and dragging a selection past an edge autoscrolls. The composition
underline and the IME anchor follow the scrolled caret. The field draws no scrollbar — it is an
editor, not a [ScrollView](scroll-view.md).

Scrolling is visual only. It never changes the value, the caret byte, the selection, the
clipboard or the undo history; a single-line field never scrolls, whatever its width.

From C++, the scroll is observable and drivable on any field:

```cpp
field->scroll_offset();          // how far the content is scrolled from its leading edge
field->maximum_scroll_offset();  // zero while the text fits
field->scroll_to(0.0f);          // clamped into [0, maximum_scroll_offset()]
```

## Notes

- The `value:` assignment is a binding the property drives; `on_changed` is how the user's
  typing reaches the property back. The pair is the standard two-way pattern
  ([Slider](slider.md) uses the same one).
- Use `on_committed` for work that should not run per keystroke — validation on Enter,
  a search executed when the field is done.
- `read_only` keeps the text selectable and focusable while refusing edits; `disabled`
  refuses everything and drops out of focus traversal.
