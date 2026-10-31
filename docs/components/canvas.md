# Canvas

A leaf drawing surface with a closed primitive vocabulary. Its `draw` block runs during the
scene-execution stage of the frame ([Rendering](../concepts/rendering.md)), so what it paints
follows the bound properties without any retained image.

## Properties

| property | type | notes |
| --- | --- | --- |
| `width` | `float` | the intrinsic width layout asks for; density-scaled, accepts `px` |
| `height` | `float` | the intrinsic height layout asks for; density-scaled, accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None.

## Children

None — a canvas is a leaf. The `draw` block is not children; it is drawing work.

## The draw block

```text
Canvas {
    width: 380px;
    height: 20px;

    draw: {
        fill_rect(0, 0, canvas_width, canvas_height, #D4D6DE);
        fill_rect(0, 0, canvas_width * level, canvas_height, #0A82FF);
        fill_rounded_rect(2, 2, canvas_width - 4, canvas_height - 4, 4, #0A82FF);
    }
}
```

- `fill_rect(x, y, width, height, color)`
- `stroke_rect(x, y, width, height, stroke_width, color)`
- `fill_rounded_rect(x, y, width, height, radius, color)`
- `stroke_rounded_rect(x, y, width, height, radius, stroke_width, color)`
- `stroke_line(x1, y1, x2, y2, stroke_width, color)`
- `fill_circle(center_x, center_y, radius, color)`
- `stroke_circle(center_x, center_y, radius, stroke_width, color)`
- `draw_text(text, x, y, font_size, color)` — a drawn axis label or legend entry
- `draw_image(@image("asset.png"), x, y, width, height, color)` — the colour modulates the image
- `measure_text(text, font_size)` — a number usable inside any coordinate, which lays the next run beside the measured one
- `canvas_width` and `canvas_height` are the canvas extent in logical units, read-only
  inside the block
- coordinates are relative to the canvas corner
- the block is re-evaluated when its bound properties change

Colour literals are `#RRGGBB` or `#RRGGBBAA`; arithmetic on the coordinates and on
`canvas_width`/`canvas_height` is ordinary float arithmetic, so a proportion of the extent —
`canvas_width * level` above — is a plain expression. `measure_text` returns the run width in
logical pixels and shapes through the same text engine the widgets use.

## Curves through retained paths

A custom render node can retain a `PathResource` whose verbs now include `QuadraticTo` and
`CubicTo` beside `MoveTo`, `LineTo` and `Close`. The reference, Skia and Blend2D replay the
curves; `render-albor` flattens them deterministically before tessellation, so a circle or a
spline does not need application-built line segments.

## When this is not enough

The vocabulary is still a closed drawing set, not a universal drawing API. A domain view — a
schematic, a waveform, a CAD frame with expensive retained content — belongs in a **custom render
node** declared through a [native component](../concepts/native-components.md) and implemented in
C++, where retained resources (`LineBatch`, `PathResource`, `PictureResource`) do the batching.
The `chart` example is the reference.
