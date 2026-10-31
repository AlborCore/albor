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
  under the pointer. Pressing the already-selected row still commits and closes the list.
- The semantic node carries role `ComboBox`, the stable `label` (or author `id`) as its name, the
  expanded state, the disabled state and the chosen state. It exposes one list item per option and
  the `open`, `close`, `select_next`, `select_previous` and `activate` semantic actions.
- When the application installs a `PopupHost` and the control belongs to an application-owned
  stage, the open list is a native `WindowRole::Popup` owned by the trigger's window. Its content is
  a themed `ListBox` in a non-activating surface placed below the trigger and kept inside the work
  area. The native surface is hidden rather than destroyed, so reopening reuses it.
- A native popup closes on Escape, an outside press, owner deactivation or application shutdown. A
  press on the trigger is forwarded to the control, which toggles the list itself. The hidden
  native surface is destroyed with the control or its owning window.
- Without a popup host or owning stage, the in-tree overlay remains the fallback. It paints above
  later siblings and receives pointer input first.
- The trigger is a focus stop; the option list is not a separate focus target.
