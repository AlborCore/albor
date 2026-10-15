# TextField

A leaf that edits a string the user types — a form field, a search box, a value editor.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `string` | the current text |
| `placeholder` | `string` | the hint shown while empty |
| `read_only` | `bool` | displays but refuses editing |
| `multiline` | `bool` | accepts line breaks |
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

## Notes

- The `value:` assignment is a binding the property drives; `on_changed` is how the user's
  typing reaches the property back. The pair is the standard two-way pattern
  ([Slider](slider.md) uses the same one).
- Use `on_committed` for work that should not run per keystroke — validation on Enter,
  a search executed when the field is done.
- `read_only` keeps the text selectable and focusable while refusing edits; `disabled`
  refuses everything and drops out of focus traversal.
