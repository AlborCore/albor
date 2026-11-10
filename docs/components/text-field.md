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
| `content_align` | `align` | where the value sits vertically inside the plate: `start`, `center`, `end` or `stretch` |
| `scrollbar` | `scrollbar` | when a bar is drawn on a multi-line field: `Scrollbar.as_needed`, `Scrollbar.off` or `Scrollbar.on` |
| `disabled` | `bool` | refuses all interaction |
| `font_size` | `float` | density-scaled; accepts `px` |
| `decorations` | `vector<TextDecoration>` | styled byte ranges for syntax highlighting and diagnostics |

`TextDecoration` carries `start` and `end` byte offsets, an optional `foreground` colour, an
optional `background` colour and an `underline` flag.

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
([Layout](../concepts/layout.md#the-two-phases)).

**Give the field a height and it becomes a fixed editor box.** The usual way to write a notes
area is a `SizedBox` with a `height`, because the field only scrolls once something caps it:

```text
SizedBox {
    height: 240px;

    TextField {
        id: notes;
        multiline: true;
        value: notes_text;
        on_changed: { notes_text = value; }   // the same two-way pattern as the single-line case
    }
}
```

A `ScrollView` around the field is neither required nor useful: the field owns its viewport, and
a `ScrollView` measures it against the viewport it just made room for, so the wrapper ends up with
nothing to scroll. Write the `SizedBox`, not the wrapper.

That holds when the field is the view's content child. A field nested deeper, inside a `Column`
the view scrolls, is measured by that container with no ceiling and grows to its whole run, as
it does anywhere else. The recipe is a height cap, never a wrapper.

### Scrolling

The caret drives the scroll: moving, typing or pasting the caret onto a line outside the box
brings it back into view, and dragging a selection past an edge autoscrolls. The wheel scrolls
the view without moving the caret, and a notch at either end is left to an enclosing viewport
instead of being swallowed. `PageUp` and `PageDown` move the caret a page and carry the view with
it, keeping the reader's line of context; `Ctrl+Home` and `Ctrl+End` reach the ends of the
document. The composition underline and the IME anchor follow the scrolled caret.

The field draws its own bar in a reserved gutter, so the text never reflows when a bar appears:

| `scrollbar` | what is drawn |
| --- | --- |
| `as_needed` (default) | the gutter is reserved, and the thumb appears while the run overflows |
| `off` | no gutter and no bar, while the wheel and the keys still scroll |
| `on` | the gutter and a thumb, whether or not the run overflows |

The gutter is reserved whenever the field is multi-line and the policy is not `off`. That is
deliberate: a bar that reserves its gutter only when needed would rewrap every line at the
moment the document crossed the box height, which is exactly while the user is typing the line
that crossed it.

Scrolling is visual only. It never changes the value, the caret byte, the selection, the
clipboard or the undo history; a single-line field never scrolls, reserves no gutter and leaves
the wheel unclaimed, whatever its width.

From C++, the scroll is observable and drivable on any field:

```cpp
field->scroll_offset();          // how far the content is scrolled from its leading edge
field->maximum_scroll_offset();  // zero while the text fits
field->scroll_to(0.0f);          // clamped into [0, maximum_scroll_offset()]
field->scroll_by(40.0f);         // a relative move, which reports whether it moved
field->owns_vertical_scroll();   // true for a multi-line field, which a container respects
```

## Content alignment

The value sits inside the plate according to `content_align`. The alignment is vertical only, so
the run always begins at the leading inset:

| member | where the run sits |
| --- | --- |
| `start` | the first line at the plate top |
| `center` | centred in the height the run does not fill |
| `end` | the last line at the plate bottom |
| `stretch` | the plate top, because a run cannot stretch to fill it |

While `content_align` is unset, the mode decides: a single-line field centres its line in the
plate, which is what a form field wants when its row hands it spare height, and a multi-line field
pins its first line to the top, which is what an editor surface wants:

```text
TextField {
    id: netlist_editor;
    multiline: true;
    content_align: Align.center;   // an explicit override; `start` is the multi-line default
    value: netlist;
}
```

A run that overflows the plate has no spare height to place itself in, so every member scrolls
from the plate top. The alignment never changes the measurement, the scroll range or the caret
bytes; the caret, the selection, the hint and the composition underline all follow the run.

From C++ the alignment is the same surface:

```cpp
field->set_content_align(Align::Start);
field->content_align();          // Align::Inherit until one is authored, and the mode answers for that
```

## Styled ranges

A decoration paints over the shaped run the field already owns; it does not reshape the value
or introduce a second text engine. Byte offsets address the value, and the range shares the
caret, selection and scroll geometry:

```text
TextField {
    value: netlist_line;
    decorations: [
        TextDecoration {
            start: 0,
            end: 4,
            foreground: #2E86DE,
            underline: false
        }
    ];
}
```

From C++ the same surface is available without a `.ui` declaration:

```cpp
field->set_decorations({
    TextDecoration{.start = 0, .end = 4, .foreground = Color::rgb(0.18f, 0.52f, 0.87f)},
    TextDecoration{.start = 9, .end = 14, .underline = true},
});
field->clear_decorations();
```

## Notes

- The `value:` assignment is a binding the property drives; `on_changed` is how the user's
  typing reaches the property back. The pair is the standard two-way pattern
  ([Slider](slider.md) uses the same one).
- Use `on_committed` for work that should not run per keystroke — validation on Enter,
  a search executed when the field is done.
- A single-line field commits on Enter and leaves the key unhandled, so a dialog's default button
  follows. A multi-line field inserts the break and claims the key instead.
- `read_only` keeps the text selectable and focusable while refusing edits; `disabled`
  refuses everything and drops out of focus traversal.
