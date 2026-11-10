# Button

A leaf push control. Pressing it raises `clicked`; binding a [command](../concepts/commands.md)
to `action` makes the button follow that command's label, enablement and click target.

## Properties

| property | type | notes |
| --- | --- | --- |
| `text` | `string` | the button caption; accepts `@tr` |
| `text_align` | `align` | the horizontal placement of the caption (and icon group); defaults to centred |
| `tooltip` | `string` | the accessible name of an icon-only button, which a future hover surface will paint; accepts `@tr` |
| `icon` | `image` | an optional retained icon drawn beside the caption |
| `icon_size` | `float` | the square icon extent; defaults to 16 logical pixels |
| `background` | `color` | the plate colour; defaults to the theme |
| `disabled` | `bool` | refuses interaction while true |
| `default` | `bool` | in a dialog, a plain Return runs this button when nothing else claimed it |
| `cancel` | `bool` | in a dialog, a plain Escape runs this button when nothing else claimed it |
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

## Aligning the caption

A button centres its caption by default. A full-width row — for example a sidebar entry —
pins the caption with `text_align`:

```text
Column {
    Button {
        text: ".TRAN";
        horizontal_align: Align.stretch;   // the plate takes the column width
        text_align: Align.start;           // the caption sits at the leading edge
        background: #2F6FED33;
    }
}
```

`text_align: Align.end` pins it to the trailing edge, and `Align.stretch` reads as leading
because a single run cannot stretch. The alignment moves the whole content group, so an icon
and its caption travel together, and the theme's horizontal button padding is honoured on
both edges.

## Notes

- `action:` and `on_clicked:` are two different tools: the first hands the whole action to a
  shared command; the second reacts locally. A button may carry either.
- A `disabled` button still occupies its measured size; it simply refuses interaction and,
  through a command, publishes that refusal to every other consumer.
- An icon-only button uses its `tooltip` as the accessible name, then its author `id`, because a
  screen reader has no caption to speak. An icon-plus-label button keeps the caption as its name.
  A labelled button keeps its caption as the name, so its tooltip is retained but not announced. The
  hover surface that paints the tooltip arrives later; the semantic name is authoritative now.
- `text_align` is separate from `horizontal_align`: the former places the caption inside the
  plate; the latter places the plate inside its parent.
- A button with no `icon` keeps the exact text-only measurement and paint path.
- In a `Dialog` window, `default: true` marks the control a plain Return runs, and
  `cancel: true` marks the control a plain Escape runs. A key the focused control claimed is
  never overridden, so a multi-line field still inserts its break. See
  [Windows and dialogs](../consuming.md#windows-and-dialogs).
- The flag also selects the preset's primary plate: a preset that fills `button_plate_default`
  paints the default control with its accent and its own `button_label_default`. A theme that
  leaves that role transparent keeps the neutral plate; the derived preset is accent throughout, so
  its default control is filled by the plain button role. An authored `background` wins over the
  whole role, and a role with no label falls back rather than hiding its caption.
