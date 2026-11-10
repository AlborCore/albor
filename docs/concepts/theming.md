# Style and theming

Theming in Albor is a shared palette object installed over a window's tree, with light and
dark accent sets and a small number of control-shape presets. Widgets read the theme; the
application owns the choice.

## Accents

A theme is built from **accents** — the four colours every surface reads against:

```cpp
albor::Accents light{.surface    = albor::Color::rgb(0.925f, 0.925f, 0.930f),
                     .on_surface = albor::Color::rgb(0.18f, 0.18f, 0.20f),
                     .accent     = accent,
                     .outline    = albor::Color::rgb(0.55f, 0.57f, 0.60f)};

albor::Accents dark{.surface    = albor::Color::rgb(0.125f, 0.125f, 0.135f),
                    .on_surface = albor::Color::rgb(0.92f, 0.94f, 0.96f),
                    .accent     = accent,
                    .outline    = albor::Color::rgb(0.42f, 0.44f, 0.48f)};
```

| field | meaning |
| --- | --- |
| `surface` | the window and panel background |
| `on_surface` | text and icons drawn against the surface |
| `accent` | the system accent — selection, focus, active states |
| `outline` | borders, separators, the edges of controls |

An application usually takes the accent from the system rather than inventing one — see
[System appearance](#system-appearance).

## Presets

A preset shapes how controls *look* — their metrics, corner treatments and state feedback —
independently of the colours:

| preset | reads as |
| --- | --- |
| `ThemePreset::Derived` | the portable baseline (default) |
| `ThemePreset::Macos` | the macOS control set |
| `ThemePreset::Fluent` | the Windows control set |
| `ThemePreset::ProDark` | a professional tool surface |

Colours and shape are separate concerns on purpose: an application can restyle the accents
of a native-looking surface without re-implementing its controls.

### What a preset decides

Beyond control metrics, a preset decides the surfaces a form is built from:

| surface | token | macOS | Windows (fluent) | derived |
| --- | --- | --- | --- | --- |
| panel / card plate | `panel_background` + `panel_border_width` | white plate, no border | fluent face with its one point line | the palette plate, unchanged |
| field well | `text_field_plate` | white or lifted well, hairline | white or lifted well, hairline | white or lifted well, outline edge |
| scrollbar | `scrollbar_overlay`, `scrollbar_hover_extent`, `scrollbar_overlay_inset` | 7px overlay, 11px hovered | 12px gutter | 10px gutter |
| default control | `button_plate_default`, `button_label_default`, `button_border_default` | accent plate, white label | accent plate, white label | empty, so the accent button plate carries it |
| vertical tab selection | `tab_selection_plate`, `tab_selection_label`, `tab_row_inset`, `tab_row_radius` | accent row, white label | indicator | indicator |

Every *new* role and metric is silent by default: a transparent role and a zero metric leave the
widget drawing exactly what it drew before, which is how the presets shape a surface without a
single `is_macos()` branch in a widget. The derived preset opts in explicitly to a field well and
its outline edge, and leaves the default-control role empty, so a derived default button falls back
to the accent plate every derived button already carries. A custom `Theme` that leaves the default
control role transparent keeps the neutral plate, and an authored `background` on a button wins
over the role.

A preset that fills `tab_selection_plate` paints a *vertical* strip's selection as a row and
suppresses the 2px indicator; a top strip always keeps the indicator. A composed `TabTitle` over a
row supplies its own colours, because a label inside it resolves its own role.

`Theme::pro_dark` is a tool surface rather than a platform preset: it keeps its neutral control
faces, leaves the default-control role empty (so its OK button keeps the tool face) and keeps its
field on the surface.

## Installing a theme

A `ThemeController` owns the theme a whole window tree borrows:

```cpp
albor::Window* window = application.window(handle);

// the starting scheme is the one the system reports
const albor::SystemAppearance appearance = window->system_appearance();

// light and dark accents, macOS-shaped controls, the reported system accent
albor::ThemeController controller(light, dark, albor::ThemePreset::Macos);

// install over the window's tree; the scheme picks which accent set is live
controller.install(*application.root(handle), appearance.scheme);

// the surface the scene clears to, read from the same theme
application.set_window_background(handle, controller.theme().surface());
```

The controller installs over the tree once; from then on widgets resolve colours through it unless
the author wrote an explicit colour. A subtree built later, such as a conditional body or a repeated
row, receives the live theme when it joins the window, so no frame hook is needed. The controller
must outlive the tree, because a widget borrows the theme rather than owning it. A scheme switch
repaints against the other accent set without rebuilding the tree.

## System appearance

```cpp
const albor::SystemAppearance appearance = window->system_appearance();
```

`SystemAppearance` carries what the OS reports — the current `scheme` (light or dark) and
the user's `accent`. An application that follows the system passes both through, as the
greeting example does; an application with its own settings can ignore them and install a
fixed scheme.

## Per-element colours

Widgets that paint themselves also expose their colours as properties, so a one-off
override is an ordinary binding:

```text
Label { text: title; foreground: #0A82FF; }
Button { text: go_label; background: #0A82FF; }
Slider { track_color: #D4D6DE; thumb_color: #0A82FF; }
CheckBox { outline: #0A82FF; check_fill: #0A82FF; }
```

Colour literals are `#RRGGBB` or `#RRGGBBAA`; the colour operations
([Expressions and bindings](../ui-language/expressions.md#colour-operations)) compose them.

An authored colour is an explicit override: it wins over the theme's role entry, while a property
the author did not write keeps resolving by role and state. The authored value is the base every
state derives from, so hover and focus lift it, press sinks it, and the disabled state dims it
because a disabled control must still read disabled. An authored colour survives a scheme flip, so
the application owns the contrast of a one-off plate.

The [rendering](rendering.md) page covers the pipeline these colours travel through.
