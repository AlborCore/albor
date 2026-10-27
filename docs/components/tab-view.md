# TabView

A container that shows one page at a time and owns the single selection of its tabs. The strip
sits above the page, or down its left side when `vertical` is set, and the selected page fills the
rest of the control.

## Properties

| property | type | notes |
| --- | --- | --- |
| `selected` | `int` | the index of the page the view shows; clamped to the pages it holds |
| `vertical` | `bool` | `true` stacks the tabs down the left side; `false` (the default) lays them across the top |

Plus the [common properties](index.md#common-properties).

## Signals

| signal | parameters | when |
| --- | --- | --- |
| `selection_changed` | `index: int` | the user or a binding moved the selection |

## Tab

A `TabView` holds zero or more `Tab` pages. Only direct `Tab` children become tabs; other children
are ignored by the strip. The view owns each page's `visible` flag, so selecting a page shows it
and hides the rest; an author-set `visible` on a `Tab` is replaced by the selection.

| property | type | notes |
| --- | --- | --- |
| `label` | `string` | the title the strip draws; also the accessible name |

A `Tab` accepts children, which are the page content. Only the selected page's content is drawn,
hit-tested or exposed to assistive technology.

## TabTitle

A `Tab` may carry one direct `TabTitle` child. The strip measures it, draws it in the header and
keeps it out of the page body. Its children are ordinary elements, so a title can be a composed
run of labels, a canvas icon, or a row of both:

```text
Tab {
    label: @tr("Settings");
    TabTitle {
        Row {
            spacing: 6px;
            Label { text: "★"; }
            Label { text: @tr("Settings"); }
        }
    }
    Column { /* page content */ }
}
```

The `label` stays the accessible name even when a `TabTitle` is present. A title is decorative:
it is painted by the strip and takes no pointer input or focus of its own, so the tab header
remains the click target. A hidden `TabTitle` falls back to the plain `label`.

## Example

```text
component DownloadPanel {
    inout property<int> current_page: 0;

    TabView {
        selected: current_page;
        on_selection_changed: {
            current_page = index;
        }

        Tab {
            label: @tr("Download");
            ProgressBar { value: 0.4; }
        }

        Tab {
            label: @tr("History");
            ListBox { selected: 0; }
        }
    }
}
```

## Layout

The strip thickness, the tab insets, the minimum tab width and the indicator thickness come from
the theme (`ControlMetrics::tab_strip_height`, `tab_padding_horizontal`, `tab_minimum_width` and
`tab_indicator_height`). Only the selected page contributes to the measured extent; the other
pages are arranged beside or under the same strip but hidden.

A horizontal strip is one tab thickness tall and as wide as its tabs put together, with the
indicator on the bottom edge of the selected header. A vertical strip is as wide as its widest
tab and as tall as its tabs stacked, with the indicator on the left edge of the selected header.
A strip longer than the view is clipped, not scrolled.

## Vertical example

```text
component SettingsPanel {
    TabView {
        vertical: true;
        selected: 0;

        Tab {
            label: @tr("General");
            TabTitle {
                Row {
                    spacing: 6px;
                    Label { text: "⚙"; }
                    Label { text: @tr("General"); }
                }
            }
            Column { /* settings */ }
        }

        Tab {
            label: @tr("Account");
            Row { /* page content */ }
        }
    }
}
```

## Semantics

The view announces the `tab_view` role. Each `Tab` announces `tab_item` with its label, its
header bounds and the `selected` state. The view exposes the actions `select_next`,
`select_previous`, `select_first` and `select_last`; each page exposes `activate`. Only the
selected page exposes its content as semantic children.

On macOS the roles map to `NSAccessibilityTabGroupRole` and `NSAccessibilityRadioButtonRole`. The
Windows UI Automation mapping is `UIA_TabControlTypeId` and `UIA_TabItemControlTypeId` as the
Windows bridge lands.

## Input

A pointer press on a header selects that page. When the view holds the keyboard focus, the left
and up arrows select the previous page, the right and down arrows select the next one, and Home
and End select the first and last page.
