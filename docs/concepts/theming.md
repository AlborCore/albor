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

The controller installs over the tree once; from then on widgets resolve colours through it.
A scheme switch repaints against the other accent set without rebuilding the tree.

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

The [rendering](rendering.md) page covers the pipeline these colours travel through.
