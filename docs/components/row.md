# Row

A container that lays its children out left to right.

## Properties

| property | type | notes |
| --- | --- | --- |
| `spacing` | `float` | the gap between consecutive children; density-scaled, accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Zero or more, in source order.

## Example

```text
component Toolbar {
    in property<command> save;

    Row {
        id: toolbar;
        spacing: 8px;

        Button { text: @tr("Run"); action: save; }

        // stretch absorbs the leftover width, pushing what follows to the far end
        Label { horizontal_align: Align.stretch; }

        Label { text: status_text; }
    }
}
```

## Notes

- `horizontal_align` on a child is its position on the row's **main axis**; `vertical_align`
  is the cross axis ([Layout](../concepts/layout.md#containers)).
- `Align.stretch` on the main axis marks a child **flexible**: it absorbs spare width and,
  when the fixed children overflow, it is measured against the width they leave. A fixed
  child keeps its authored width even if the row then overflows.
- The spacer idiom — a stretch-aligned child with no natural size pushing its siblings to
  the far end — is the standard trailing-status pattern, as in the toolbar above.
- A `ScrollView` with no `viewport_width` fills the width a row leaves only when it declares
  `horizontal_align: Align.stretch`. Without it the scroll view is a fixed child and measures
  the whole offered width.
