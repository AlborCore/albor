# CheckBox

A leaf that carries a truth value the user toggles — an option, an armed state, a
selection flag.

## Properties

| property | type | notes |
| --- | --- | --- |
| `checked` | `bool` | the current state |
| `disabled` | `bool` | refuses interaction |
| `outline` | `color` | the box border colour; defaults to the theme |
| `check_fill` | `color` | the mark colour; defaults to the theme's accent |
| `action` | `command` | binds a shared command |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `toggled` | `checked: bool` | the user flips it — carries the new state |

## Children

None — a checkbox is a leaf.

## Example

```text
component ArmedToggle {
    inout property<bool> armed: false;

    Column {
        spacing: 8px;

        Label { text: @tr("check to disable the controls"); }

        CheckBox {
            id: armed_box;
            checked: armed;             // property -> checkbox: a binding
            on_toggled: {
                armed = checked;        // checkbox -> property: the signal's new state
            }
        }

        Button { text: @tr("Dangerous"); disabled: armed; }
        Slider { value: level; min: 0.0; max: 1.0; disabled: armed;
                 on_changed: { level = value; } }
    }
}
```

## Notes

- `toggled` carries the new state as its parameter, so the handler assigns directly — no
  re-reading the property to learn what just happened.
- A disabled checkbox still shows its state; it simply refuses to change it, and through a
  command publishes that refusal to every other consumer of the same object.
