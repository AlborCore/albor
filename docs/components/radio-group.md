# RadioGroup

A container that lays out radio buttons in a row or a column and owns their single choice. A
group stores one selected value rather than a checked flag per child, so the first direct
`RadioButton` whose `value` matches `selected` is checked.

## Properties

| property | type | notes |
| --- | --- | --- |
| `selected` | `int` | the value of the selected choice; no choice is marked when no child carries it |
| `vertical` | `bool` | `true` stacks the choices; `false` (the default) lays them in one row |
| `spacing` | `float` | the gap between choices; defaults to `ControlMetrics::radio_group_spacing` |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | when |
| --- | --- | --- |
| `selection_changed` | `value: int` | a press or a binding moved the selection to another value |

The signal is raised only for an actual move; writing the current `selected` value again reports
nothing. Selecting a value no child carries clears the checked mark without inventing a choice.
Give each choice a distinct `value`: when two share one, the first one is the checked choice,
and pressing the later duplicate does not report a new selection.

## Children

A `RadioGroup` lays out direct `RadioButton` children and ignores other children. The group owns
the selected value; a radio does not expose a `checked` property of its own.

## Example

```text
component ModePanel {
    inout property<int> mode: 0;

    Column {
        spacing: 8px;

        Label { text: @tr("Mode"); }

        RadioGroup {
            id: mode_group;
            selected: mode;
            spacing: 12px;

            on_selection_changed: {
                mode = value;
            }

            RadioButton { value: 0; label: @tr("Fast"); }
            RadioButton { value: 1; label: @tr("Balanced"); }
            RadioButton { value: 2; label: @tr("Safe"); }
        }
    }
}
```

## Layout

A horizontal group is as wide as its choices and the gaps between them, and as tall as its
tallest choice. A vertical group is as wide as its widest choice and as tall as every choice
stacked. The group itself draws no background; each choice draws its circle, outline, dot and
label.

The circle extent, the outline width and the circle-to-label gap come from
`ControlMetrics::radio_button_size`, `radio_button_outline` and `radio_button_gap`. The default
group gap comes from `radio_group_spacing`; writing `spacing` overrides it for that group.

## Input

A direct `RadioButton` claims a pointer press over its whole row, including the label. A focused
choice also selects on Space. A disabled choice draws its dimmed state and refuses every path.
The group's semantic actions `select_next` and `select_previous` move the selection without a
keyboard chord of their own.

## Semantics

The group announces the `radio_group` role with its author-assigned `id`. Each direct choice
announces `radio_button` with its label as the accessible name, its bounds, the `checked` state
and the `selected` state. The group exposes `select_next` and `select_previous`; each choice
exposes `activate`.

On macOS the roles map to `NSAccessibilityRadioGroupRole` and `NSAccessibilityRadioButtonRole`.
On Windows UI Automation they map to `UIA_GroupControlTypeId` and
`UIA_RadioButtonControlTypeId`, with the selection item pattern on each choice.

# RadioButton

One choice inside a `RadioGroup`. The circle and label form one row, and the whole row is the
pointer target. The group, not the button, writes the chosen state.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `int` | the value the owning group matches against `selected`; defaults to `0` |
| `label` | `string` | the row text; also the accessible name |
| `disabled` | `bool` | dims the choice and refuses input |

Plus the [common properties](index.md#common-properties).

## Signals

None. The owning `RadioGroup` raises `selection_changed`.

## Children

None — a radio button is a leaf.

## Semantics

The choice announces `radio_button`, its label, its bounds, whether it is checked, whether it is
selected, and the `activate` action. A disabled choice reports the disabled state. Values should
be distinct, because the group selects by value rather than by button identity.
