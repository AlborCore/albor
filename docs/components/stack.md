# Stack

A container that overlays its children — each one arranged over the same bounds, later
children above earlier ones. A stack is the layering primitive: a background under a
foreground, a badge over a plate.

## Properties

None of its own, beyond the [common properties](index.md#common-properties).

## Signals

None.

## Children

Zero or more, layered in source order — the last child is on top.

## Example

```text
component Badge {
    in property<string> label_text: "";
    inout property<bool> show_badge: true;

    Stack {
        id: badge_stack;

        // layer 1: the plate
        Canvas {
            width: 24px;
            height: 24px;
            draw: {
                fill_rounded_rect(0, 0, canvas_width, canvas_height, 6, #0A82FF);
            }
        }

        // layer 2: the mark, centred over the plate
        Label {
            horizontal_align: Align.center;
            vertical_align: Align.center;
            text: label_text;
            foreground: #FFFFFF;
            visible: show_badge;
        }
    }
}
```

## Notes

- Children are measured and arranged against the same rectangle — a stack does not flow
  them. Alignment is how an overlay is positioned within the shared bounds.
- `visible: false` on the top layer removes it from the stack without disturbing the layers
  below ([control flow](../ui-language/control-flow.md#visible-hide-in-place)).
