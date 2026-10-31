# Button

A leaf push control. Pressing it raises `clicked`; binding a [command](../concepts/commands.md)
to `action` makes the button follow that command's label, enablement and click target.

## Properties

| property | type | notes |
| --- | --- | --- |
| `text` | `string` | the button caption; accepts `@tr` |
| `icon` | `image` | an optional retained icon drawn beside the caption |
| `icon_size` | `float` | the square icon extent; defaults to 16 logical pixels |
| `background` | `color` | the plate colour; defaults to the theme |
| `disabled` | `bool` | refuses interaction while true |
| `action` | `command` | binds a shared command; the command's state drives the control |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `clicked` | — | the button is pressed and released |

## Children

None — a button is a leaf.

## Example

```text
component Toolbar {
    in property<command> save;
    inout property<int> presses: 0;

    Row {
        spacing: 8px;

        Button {
            id: save_button;
            text: @tr("Save");
            action: save;                    // the command drives label, enablement and click
        }

        Button {
            text: @tr("Press me");
            on_clicked: {
                presses = presses + 1;       // a handler: this component's own state
            }
        }

        Button {
            text: @tr("Armed-only");
            disabled: !armed;                // a binding: disabled follows the property
        }
    }
}
```

## Notes

- `action:` and `on_clicked:` are two different tools: the first hands the whole action to a
  shared command; the second reacts locally. A button may carry either.
- A `disabled` button still occupies its measured size; it simply refuses interaction and,
  through a command, publishes that refusal to every other consumer.
- An icon-only button uses its author `id` as the accessible name, because a screen reader has no
  caption to speak. An icon-plus-label button keeps the caption as its name.
- A button with no `icon` keeps the exact text-only measurement and paint path.
