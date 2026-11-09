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

- `vertical_align` on a child is its position on the column's **main axis**;
  `horizontal_align` is the cross axis ([Layout](../concepts/layout.md#containers)).
- `Align.stretch` on the main axis marks a child **flexible**: it absorbs spare height and,
  when the fixed children overflow, it is measured against the height they leave. A fixed
  child keeps its authored height even if the column then overflows.
- A child with no natural size and `vertical_align: Align.stretch` is the spacer idiom —
  it takes whatever height the column does not use.
- A `ScrollView` with no `viewport_height` fills the height a column leaves only when it
  declares `vertical_align: Align.stretch`. Without it the scroll view is a fixed child and
  measures the whole offered height.
