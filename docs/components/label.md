# Label

A leaf that draws a run of text — a caption, a title, a value readout.

## Properties

| property | type | notes |
| --- | --- | --- |
| `text` | `string` | the run to draw; accepts `@tr` and any string expression |
| `foreground` | `color` | the text colour; defaults to the theme's `on_surface` |
| `font_size` | `float` | density-scaled; accepts `px` |

Plus the [common properties](index.md#common-properties).

## Signals

None. A `Label` is passive; wrap it in a [Button](button.md) to make it interactive.

## Children

None — a label is a leaf.

## Example

```text
Column {
    spacing: 4px;

    Label {
        text: @tr("Hello, ") + user_name;
        font_size: 18px;
    }

    Label {
        text: @tr("Count: ") + string(click_count);
        foreground: #0A82FF;
    }

    // a stretched label is the spacer idiom's filler — it takes the leftover width
    Label { horizontal_align: Align.stretch; }
}
```

## Notes

- Text measurement goes through the shaping engine, so a label's intrinsic size is the
  shaped run, not a character count.
- An empty label still occupies its line height; `visible: false` removes it from layout
  entirely.
