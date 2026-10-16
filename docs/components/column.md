# Column

A container that stacks its children top to bottom.

## Properties

| property | type | notes |
| --- | --- | --- |
| `spacing` | `float` | the gap between consecutive children; density-scaled, accepts `px` |
| `padding` | `float` | the inset around the whole stack; density-scaled, accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Zero or more, stacked in source order.

## Example

```text
component Greeting {
    in property<string> user_name: "friend";
    inout property<int> click_count: 0;

    Column {
        id: root;
        spacing: 8px;
        padding: 16px;

        Label {
            text: @tr("Hello, ") + user_name;
            font_size: 18px;
        }

        Label { text: @tr("Count: ") + string(click_count); }

        Button {
            text: @tr("Increment");
            on_clicked: {
                click_count = click_count + 1;
            }
        }

        // stretch makes this child absorb the leftover height below it
        Label { vertical_align: Align.stretch; }
    }
}
```

## Notes

- `vertical_align` on a child is the child's position **within** its column slot — it is a
  screen axis, never the column's main axis
  ([Layout](../concepts/layout.md#containers)).
- A child with no natural size and `vertical_align: Align.stretch` is the spacer idiom —
  it takes whatever height the column does not use.
