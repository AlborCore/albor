# ComboBox

A leaf that chooses one value from a compact list. The trigger shows the selected option; pressing
it opens the option list, and the keyboard can drive the same choice without a pointer.

## Properties

| property | type | notes |
| --- | --- | --- |
| `items` | `vector<string>` | the options in display order |
| `selected` | `int` | the chosen index; negative clears the selection |
| `label` | `string` | the stable accessible name; defaults to the author `id` |
| `placeholder` | `string` | the hint shown while nothing is selected |
| `disabled` | `bool` | refuses the trigger and the popup |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `selection_changed` | `index: int` | the user commits a different option |

## Children

None — the options are the `items` strings, not child elements.

## Keyboard

- `Down` and `Up` open the list and move the highlight.
- `Home` and `End` jump to the first and last option.
- `Return` or `Space` commits the highlighted option.
- `Escape` closes the list without changing the selection.
- A press outside the open popup closes it.

## Example

```text
component PlotSettings {
    inout property<int> mode: 0;

    ComboBox {
        id: plot_mode;
        items: [@tr("Lines"), @tr("Points"), @tr("Bars")];
        selected: mode;
        label: @tr("Plot mode");
        placeholder: @tr("Choose a plot mode");
        on_selection_changed: {
            mode = index;
        }
    }
}
```

## Notes

- The trigger shows the selected string; the popup highlights both the chosen row and the row
  under the pointer.
- The semantic node carries role `ComboBox`, the stable `label` (or author `id`) as its name, the
  expanded state, the disabled state and the chosen state. It exposes one list item per option.
- The popup is an in-tree overlay layer added to the topmost root, so it paints above later siblings
  and receives pointer input first. It is still not a native `WindowRole::Popup` surface; that
  native parity is the remaining #18 gap.
- The trigger is a focus stop; the option list is not a separate focus target.
