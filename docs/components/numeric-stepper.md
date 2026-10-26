# NumericStepper

A leaf that shows a bounded number and steps it with a minus and a plus. The value is always
clamped into the range, so a binding cannot drive the control outside what it offers.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `float` | the current number; clamped into `[min, max]` |
| `min` | `float` | the lower bound; defaults to `0` |
| `max` | `float` | the upper bound; defaults to `100` |
| `step` | `float` | how far one press, wheel notch or key moves; defaults to `1` |
| `disabled` | `bool` | dims the plate and refuses input |

Plus the [common properties](index.md#common-properties).

Writing a `min` above the current `max` moves `max` up with it, and writing a `max` below `min`
moves `min` down, because a range has no inverted state. The value is clamped after either one.
A negative `step` is stored as its absolute value, because the side zones own the direction. A
zero `step` is stored as `0.0001` rather than freezing the control.

## Signals

| signal | parameters | when |
| --- | --- | --- |
| `changed` | `value: float` | a press, a wheel notch, a key or a binding moved the value |

The signal fires only for an actual change. A step that would leave the range reports nothing
when the value is already at that end.

## Children

None — a numeric stepper is a leaf.

## Example

```text
component EffortPanel {
    inout property<float> effort: 30.0;

    Column {
        spacing: 8px;

        Label { text: @tr("Effort") + " " + string(effort); }

        NumericStepper {
            id: effort_stepper;
            value: effort;
            min: 0.0;
            max: 100.0;
            step: 5.0;

            on_changed: {
                effort = value;
            }
        }
    }
}
```

## Formatting

The plate draws the value in its shortest decimal form: trailing zeros and a bare decimal point
are removed, so `12.0` reads `12` and `3.5` reads `3.5`. A fractional value keeps up to six
decimals, so a magnitude below `0.0000005` rounds to `0`.

## Layout

The plate measures to at least `ControlMetrics::numeric_stepper_extent` and grows to fit the
value run between the two step zones. The zone width is `numeric_stepper_button_width`, the
inset around the run is `numeric_stepper_padding`, and the plate corner comes from the theme's
`corner_radius`.

## Input

A pointer press in the left or right zone steps by one `step`; a press on the value area does
nothing. A wheel notch over the plate steps in the wheel's direction. When the stepper holds the
keyboard focus:

| key | action |
| --- | --- |
| Up, Right | one step up |
| Down, Left | one step down |
| PageUp, PageDown | ten steps up or down |
| Home, End | the lower or upper bound |

A disabled stepper refuses every path above.

## Semantics

The stepper announces the `numeric_stepper` role, its author-assigned `id`, its formatted value
and the disabled state. It exposes `increment` and `decrement` actions.

On macOS the role maps to `NSAccessibilityIncrementorRole`, the value is an `NSNumber`, and the
bridge answers `accessibilityPerformIncrement` and `accessibilityPerformDecrement`. On Windows UI
Automation the role maps to `UIA_SpinnerControlTypeId` and the value is exposed read-only through
the value pattern; increment and decrement through UI Automation are not exposed yet.
