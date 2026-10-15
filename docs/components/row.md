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

- `horizontal_align` on a child positions it **within** its row slot — a screen axis, never
  the row's main axis ([Layout](../concepts/layout.md#containers)).
- The spacer idiom — a stretch-aligned child with no natural size pushing its siblings to
  the far end — is the standard trailing-status pattern, as in the toolbar above.
