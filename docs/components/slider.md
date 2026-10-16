# Slider

A leaf that carries a continuous value the user drags — a volume control, a level, a
parameter scrubber.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `float` | the current position |
| `min` | `float` | the low end of the range |
| `max` | `float` | the high end of the range |
| `disabled` | `bool` | refuses interaction |
| `track_color` | `color` | the rail colour; defaults to the theme |
| `thumb_color` | `color` | the handle colour; defaults to the theme's accent |
| `action` | `command` | binds a shared command |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | raised when |
| --- | --- | --- |
| `changed` | `value: float` | the user drags the thumb — continuously |

## Children

None — a slider is a leaf.

## Example

```text
component LevelControl {
    inout property<float> level: 0.5;

    Column {
        spacing: 8px;

        Label { text: string(level * 100.0) + "%"; }

        Slider {
            id: level_slider;
            value: level;          // property -> slider: a binding
            min: 0.0;
            max: 1.0;
            on_changed: {
                level = value;     // slider -> property: the signal's named parameter
            }
        }
    }
}
```

## Notes

- `value:` is a binding, so a property write from anywhere — a timer, a command, another
  control — moves the thumb. `on_changed` is the reverse direction, written by hand; the
  two together are the standard two-way pattern.
- `min`/`max` are ordinary floats; nothing stops `min` from exceeding `max` in the data —
  keep the range consistent.
- `thumb_color` defaulting to the accent is what makes a slider read as the primary control
  on a surface without extra styling.
