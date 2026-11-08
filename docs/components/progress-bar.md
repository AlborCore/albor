# ProgressBar

A read-only indicator that shows how far a task has advanced. The value is normalized:
`0` is empty, `1` is full, and any value outside that range is clamped.

## Properties

| property | type | notes |
| --- | --- | --- |
| `value` | `float` | the fraction of the bar that is filled, in the range `[0, 1]` |
| `indeterminate` | `bool` | shows a repeating sweep for work whose remaining length is unknown; `value` is ignored |
| `disabled` | `bool` | draws the dimmed track and fill; the bar takes no input either way |
| `track_color` | `color` | the unfilled track colour; defaults to the theme |
| `fill_color` | `color` | the progress fill colour; defaults to the theme's accent |

Plus the [common properties](index.md#common-properties).

## Signals

None — a progress bar reports state rather than raising events.

## Children

None — a progress bar is a leaf.

## Example

```text
component DownloadPanel {
    in property<float> progress: 0.0;

    Column {
        spacing: 8px;

        Label { text: @tr("Downloading"); }
        ProgressBar { id: download_progress; value: progress; }
    }
}
```

Conversion from a byte count is the application's decision, because the widget takes a
fraction rather than a range:

```text
value: float(bytes_done) / float(bytes_total);
```

## Semantics

The bar announces the `progress_bar` role, its author-assigned `id`, and its current fraction. In
indeterminate mode the semantic tree marks it `indeterminate` and the name carries no fraction,
because the remaining length is unknown. It exposes no actions, because the user does not drive it,
and it is not a keyboard focus stop.
