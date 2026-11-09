# Padding

A container that insets its single child. Use it when the inset is an edge effect around a
piece of content, rather than the spacing of a whole stack.

## Properties

| property | type | notes |
| --- | --- | --- |
| `padding` | `float` or `Insets` | uniform inset on all four sides (`float`/`px`), or per-edge insets via `Insets` record |

Plus the [common properties](index.md#common-properties).

## Insets Record

To specify asymmetric padding, assign an `Insets` record specifying any of `top`, `right`, `bottom`, and `left`:

```text
Insets {
    top: 4px,
    right: 8px,
    bottom: 4px,
    left: 10px
}
```

Unspecified fields default to `0px`. For example, to specify a left-only padding:

```text
padding: Insets { left: 10px };
```

## Signals

None.

## Children

Exactly one — the content that gets the inset.

## Examples

### Uniform padding

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

### Asymmetric / per-edge padding

```text
component IndentedSection {
    Padding {
        id: indented;
        padding: Insets { left: 10px };

        Label { text: @tr("Indented content"); }
    }
}
```

## Notes

- Prefer [Column's](column.md) own `padding` when the inset wraps a whole stack — one
  property, one element. A dedicated `Padding` is for wrapping a child that is not a stack,
  or for insetting content inside a larger container.
