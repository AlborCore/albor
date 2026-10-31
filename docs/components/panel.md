# Panel and Card

A themed container that frames a section of a surface. `Panel` and `Card` are the same element:
the second spelling is the one a settings page usually reads better. Use a panel when a group of
controls needs one face, one inset and one semantic grouping rather than a bare stack.

## Properties

| property | type | notes |
| --- | --- | --- |
| `padding` | `float` | the inset between the panel edge and its children; defaults to the theme |
| `spacing` | `float` | the gap between two stacked children |
| `background` | `color` | the panel face; defaults to the theme |
| `corner_radius` | `float` | the corner radius; defaults to the theme |
| `border_width` | `float` | the border stroke; defaults to the theme |
| `border_color` | `color` | the border colour; defaults to the theme |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

Any number. The panel stacks its visible children top to bottom inside the padding, separated by
`spacing`, and measures to the widest child plus both insets.

## Example

```text
component SettingsPage {
    Panel {
        id: settings_panel;
        padding: 16px;
        spacing: 12px;

        Label { text: @tr("Waveform"); }

        Row {
            spacing: 8px;
            CheckBox { checked: show_grid; }
            Label { text: @tr("Show grid"); }
        }
    }
}
```

## Notes

- The panel paints its face, radius and border through the installed theme. An explicit property
  wins over the theme value when the author needs a one-off surface.
- The semantic tree announces a `Group`, carrying the author `id` and the panel bounds. The
  controls inside remain the children a reader walks.
- A panel is a container, so it does not take focus; the first focusable child does.
- `Card` is an alias for the same class, so a `.ui` file can choose the name that reads better
  without adding a second implementation.
