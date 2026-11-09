# SizedBox

A single-child layout container that sizes or constrains its content to an explicit width,
height, or dimensional floor/ceiling. Use it to fix caption column widths in forms, enforce
minimum hit targets, or create fixed structural gaps.

## Properties

| property | type | notes |
| --- | --- | --- |
| `width` | `float` | exact width; density-scaled, accepts `px` |
| `height` | `float` | exact height; density-scaled, accepts `px` |
| `min_width` | `float` | minimum width; guarantees content is at least this wide |
| `min_height` | `float` | minimum height; guarantees content is at least this tall |
| `max_width` | `float` | maximum width; caps child width |
| `max_height` | `float` | maximum height; caps child height |

Plus the [common properties](index.md#common-properties).

## Precedence Rules

- An exact `width` sets both the minimum and maximum width to that value, taking precedence over `min_width` and `max_width`.
- An exact `height` sets both the minimum and maximum height to that value, taking precedence over `min_height` and `max_height`.
- When `width` is unset, `min_width` establishes the floor while `max_width` establishes the ceiling.
- Child alignment inside the box respects the child's own `horizontal_align` and `vertical_align` properties (`Align.start`, `Align.center`, `Align.end`, `Align.stretch`).

## Signals

None.

## Children

Zero or one.
- When wrapping a child, `SizedBox` constrains and arranges that child.
- When empty, `SizedBox` measures as its declared dimensions, acting as a clean fixed spacer without dummy canvas nodes.

## Examples

### Fixed-width form label (aligned columns)

```text
component FormLabel {
    in property<string> text: "";

    SizedBox {
        width: 120px;

        Label {
            font_size: 13px;
            text: text;
        }
    }
}
```

### Flexible floor (prevents truncation while keeping column alignment)

```text
SizedBox {
    min_width: 120px;

    Label {
        font_size: 13px;
        text: caption_text;
    }
}
```

### Standalone structural spacer

```text
Column {
    Label { text: "Section 1"; }

    // an explicit 24px gap between sections
    SizedBox { height: 24px; }

    Label { text: "Section 2"; }
}
```
