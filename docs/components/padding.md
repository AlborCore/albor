# Padding

A container that insets its single child. Use it when the inset is an edge effect around a
piece of content, rather than the spacing of a whole stack.

## Properties

| property | type | notes |
| --- | --- | --- |
| `padding` | `float` | the inset on all four sides; density-scaled, accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Exactly one — the content that gets the inset.

## Example

```text
component Panel {
    in property<string> caption;

    Padding {
        id: panel_padding;
        padding: 12px;

        Column {
            spacing: 4px;

            Label { text: caption; }
            Label { text: @tr("some detail"); }
        }
    }
}
```

## Notes

- Prefer [Column's](column.md) own `padding` when the inset wraps a whole stack — one
  property, one element. A dedicated `Padding` is for wrapping a child that is not a stack,
  or for insetting content inside a larger container.
