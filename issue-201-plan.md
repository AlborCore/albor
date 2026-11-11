# Issue #201 — native presets do not dress the simulation dialog surfaces

**Date:** 2026-11-10
**Status:** implemented on `issue-201-native-preset-surfaces` — decisions marked *needs owner sign-off*
were settled by the owner on 2026-11-10 (see **Decisions taken**).
**Scope:** #201 only. Base commit `88b291d` (`main`).
**Related:** `xyce-studio-resolution-plan.md` (#145–#149), `gap-analysis-xyce-studio.md`.

## Verdict

The issue is real and it is a framework gap, not an application mistake. All five gaps are
palette/metric decisions that the preset already owns; none of them needs a `.ui` property, a
compiler change, a `ThemeController` setter, or a platform source. The correct fix is five palette
roles, five metrics and three resolver rules, because `ThemePreset` is not visible to widgets: the
preset communicates through `Palette` and `ControlMetrics`, and every widget already resolves by
role and state.

The change also does **not** reopen a settled decision. Presets may shape a surface (§8.4, bullet
2), a role per state is the palette contract (§6.1), and the overlay scrollbar is §8.2 layout
policy rather than a new widget. Two of the issue's own claims about the Windows preset are wrong
(see **Two claims the tree contradicts**), so the single inconsistency in the issue is resolved by
splitting the work rather than by widening it.

## What was verified

Each claim was checked against the tree rather than taken at face value.

| claim | evidence | verdict |
| --- | --- | --- |
| a `Panel` face is `lighten(surface, 0.05)` plus a 30% hairline | `apply_macos_plates` in `albor/style/theme.cpp` sets the panel face to `face` and the border to `hairline` (light alpha 0.30) | confirmed |
| `apply_derived_focus` sets `text_field_plate` to the theme surface | `apply_derived_focus` reads `theme.surface()` and writes all five plate states, then borrows `button_border` for the field line | confirmed |
| the default metrics reserve a 10px gutter | `ControlMetrics::scrollbar_extent = 10.0f` (`albor/style/theme.h`); `Theme::macos` never overrides it | confirmed |
| `Button::default` changes nothing on screen | `Button::set_default` records the flag only, and its comment says so; `plate_colors()` returns `button_plate` unconditionally | confirmed |
| a vertical strip paints its 2px marker at the left edge, before the title | `TabView::paint_content` draws the marker at `header.x` before `title->render`, and `title_bounds` insets by `tab_padding()` on both sides | confirmed |
| `ThemeController` exposes only `const Theme& theme()` | `albor/widgets/theme_controller.h`; the controller copies the live value into `m_attached->theme` so a widget's borrowed pointer survives a reinstall | confirmed |
| `Theme::fluent` paints white field wells | `apply_derived_focus` writes the field plate as `theme.surface()`, so a fluent field is the window colour, not white | **contradicted** |
| `Theme::fluent` paints an accent-filled default button | `apply_fluent_plates` writes a neutral `button_plate` and nothing else; there is no default-button role at all | **contradicted** |

### Two claims the tree contradicts

The issue's Windows section says "keep the current native behaviour … white field wells … accent-
filled default button". Neither exists today: the fluent field is the surface colour and every
fluent button is neutral. Its acceptance criterion 5 ("confirm `Theme::fluent` and `Theme::derived`
are unchanged") therefore contradicts its own expectation list.

Resolution: #201 adds the *roles* so a preset can express both. The owner decided that the Windows
and Linux presets are updated in the same change: `Theme::fluent` gains the well and the accent
primary control while keeping its twelve point gutter, and the derived baseline gains the well and
copies its accent plate into the default role, so a derived default control looks like every other
derived button. The macOS values never leak into either, and the "unchanged" test becomes a test
that locks each preset's own native values.

## Rules every change inherits

- **Preset ownership (§8.4).** The application authors no colour or metric for these surfaces. The
  preset is the only writer, so a Xyce Studio dialog keeps working on all three platforms.
- **Transparent means silent (§6.1).** A new role stays transparent by default and a new metric
  stays zero by default, so a hand-built `Theme`, an unthemed widget and the derived preset keep
  their current pixels. This is the same rule `focus_ring_outer`, `slider_fill` and `button_border`
  already follow.
- **`ThemePreset` stays out of widgets (§8.4).** No widget gains an `is_macos()` branch; every
  decision is a token the preset writes.
- **No platform source is touched.** `style/` and `widgets/` are portable C++, so the AD-16
  Windows-counterpart rule does not add a Win32 source here. `macos-windows.md` still records the
  preset status, and the Windows CI job still has to build the change.
- **No layout change under `#ifdef` (AD-02).** The overlay metric changes measured width at run
  time, not at compile time.
- **Docs are part of the change.** `Button::set_default` currently documents the flag as a dialog
  convention with no pixels attached, and the design doc never states the control's appearance;
  after this change the flag selects a palette role, so the comment, the spec and
  `docs/components/button.md` are corrected in the same pass that changes the behaviour.

## The design

### New palette roles

Six per-state tables. All default to transparent, so nothing changes until a preset fills one.

| role | read by | default | macOS |
| --- | --- | --- | --- |
| `button_plate_default[5]` | `Button::plate_colors` | transparent | accent |
| `button_label_default[5]` | `Button::label_colors` | transparent | white |
| `button_border_default[5]` | `Button::border_colors` | transparent | transparent |
| `tab_strip_background[5]` | `TabView` sidebar plate | transparent | near-white / lifted |
| `tab_selection_plate[5]` | `TabView` row plate | transparent | accent |
| `tab_selection_label[5]` | `TabView` row label | transparent | white |

The plate is the gate: a button reads the three default tables only when the plate and its label
are filled, and a strip paints a row only when `tab_selection_plate` and `tab_selection_label` are
filled, so a half filled role falls back rather than drawing invisible text. This keeps a hand-built
theme on the neutral tables and
makes the pair a contract documented on the field. `button_border_default` may be transparent on
purpose, which is why the gate is the plate and not the border.

macOS derivation (the same shape as the existing accent states):

| state | default plate | default label |
| --- | --- | --- |
| Normal | accent | white |
| Hovered | `lighten(accent, 0.08)` | white |
| Pressed | `darken(accent, 0.12)` | white |
| Disabled | accent at 0.45 alpha | white at 0.55 alpha |
| Focused | `lighten(accent, 0.04)` | white |

The tab row uses the same derivation, so a sidebar row and a default button agree.

### New metrics

| metric | meaning | default | macOS |
| --- | --- | --- | --- |
| `scrollbar_overlay` | the bar paints over the content instead of reserving a gutter | `false` | `true` |
| `scrollbar_hover_extent` | the width the bar takes while hovered or dragged, where zero keeps `scrollbar_extent` | `0.0f` | `11.0f` |
| `scrollbar_overlay_inset` | the gap between an overlay bar and the trailing edge | `0.0f` | `2.0f` |
| `tab_row_inset` | the inset of a selection row inside its header, which also yields the row height | `0.0f` | `2.0f` |
| `tab_row_radius` | the corner a selection row is drawn with | `0.0f` | `6.0f` |

`scrollbar_extent` keeps its meaning (the resting width, and the gutter when not overlay) and
becomes `7.0f` for macOS. The sidebar row needs no height metric: the macOS strip is 32px and
`tab_row_inset` 2 yields the 28px row, with `tab_row_radius` giving the 6px corner the issue asks
for.

### New macOS surfaces

The owner's follow-up comment on #201 inverted the light assignment relative to the issue: native
macOS System Settings paints a white pane, a grey group plate inside it and a near-white sidebar.
The rows below are the corrected values.

| surface | light | dark |
| --- | --- | --- |
| pane (`surface`) | `rgb(1, 1, 1)` | `surface` (unchanged) |
| panel plate | `darken(white, 0.05)` (`#F2F2F2`) | `lighten(surface, 0.10)` |
| panel border width | `0` | `0` |
| sidebar plate | `darken(white, 0.03)` (`#F7F7F7`) | `lighten(surface, 0.06)` |
| field plate | white (the pane) | `lighten(surface, 0.04)` |
| field line | `button_border` hairline (unchanged) | hairline (unchanged) |
| field focused line | accent (unchanged) | accent (unchanged) |
| scrollbar thumb | `on_surface` at normal 0.30, hover 0.55, pressed 0.70, disabled 0.15 | normal 0.35, hover 0.60, pressed 0.75, disabled 0.20 |
| scrollbar track | transparent (unchanged) | transparent (unchanged) |

A light button face is `darken(pane, 0.03)` and its hover and press darken further, because a
control on a white pane steps down rather than lifting toward white; dark keeps the lift it had.
The button line, slider, typography and every other macOS metric stay as they are. A card does not
react to hover or press, because a native content plate does not.

## Phases

Each phase compiles, is reviewable on its own, and carries its own tests, so a partial landing
still leaves the tree consistent.

### P0 — recovery point

`bash build/snapshot.sh issue-201-start`. The pass touches four public headers, four sources, four
test files and seven docs; a snapshot is cheaper than remembering to take one later.

### P1 — field surface split and macOS card/field

`apply_derived_focus` currently owns three unrelated decisions (halo, slider fill, field surface).
Split the third into `apply_field_surface(theme, plate, accent)` and call it from each preset with
its own plate, after the halo is applied. The line stays `button_border`, which is what every preset
reads today, so only the plate moves:

- `macos`: white plate light / `lighten(surface, 0.04)` dark, with the existing hairline and accent
  focus line.
- `fluent`: a white or lifted well of its own, because a fluent text box is a well.
- `pro_dark`: `theme.surface()` as the plate, which is exactly what it reads today, so its pixels do
  not move.

Then in `apply_macos_plates`: panel plate per the table above, all pointer states equal, disabled at
half alpha, panel border transparent at every state; `apply_macos_metrics` sets
`panel_border_width(0)`. Keep the panel radius at 6 and the padding at 12.

Files: `albor/style/theme.h`, `albor/style/theme.cpp`.
Tests (`tests/theme.test.cpp`): the macOS panel plate is white/lifted and differs from
`button_plate`; the panel border width is zero; the field plate differs from the surface and from
the panel in dark mode; the field keeps the accent focus line; fluent gains a well of its own,
pro-dark keeps `text_field_plate == theme.surface()`, derived gains its well and outline edge, and
fluent and derived keep `panel_border_width == 1`.

### P2 — default button role

`albor/style/theme.h`: the three default tables and their setters. `albor/widgets/button.cpp`: the
three resolver helpers pick the default tables when `m_default` is set and the role is filled;
`set_default` now calls `invalidate()` because it changes pixels, and its comment changes with it.

Files: `albor/style/theme.h`, `albor/style/theme.cpp`, `albor/widgets/button.h`,
`albor/widgets/button.cpp`.
Tests: a macOS default button records the accent fill and the white label; the same button with
`default: false` records the neutral face; an authored `background` still wins over the role; a
fluent default button paints the accent while a plain fluent button keeps its neutral face, and a
derived default button falls back to the accent plate because its default role stays empty.

The authored path already behaves: `resolve_authored` reads the table whenever `m_background_set` is
false, so the preset's four states are honoured as written, and an authored plate still wins over
the default role. No extra branch is needed there; the only new branch is which table is handed to
the resolver.

### P3 — overlay scrollbar

`albor/widgets/scroll_view.cpp`:

- rename `m_bar_reserved` to `m_bar_needed`, because it now records "the content overflows" and no
  longer means "the gutter is taken".
- `bar_reserved()` answers false when `scrollbar_overlay`, so `content_viewport()` returns the full
  width and the content lays out 7px wider.
- `measure()` keeps the full-width probe, records `m_bar_needed`, and runs the narrowed second pass
  only when no overlay is active. `scrollbar_visible()` keeps reading `m_bar_needed`, so an
  `as_needed` overlay bar still appears while the content overflows.
- `drawn_bar_extent()` takes `scrollbar_hover_extent` while `hovered() || m_dragging_thumb`, and
  `scrollbar_track_rect()` / `thumb_rect()` subtract `scrollbar_overlay_inset` from the trailing
  edge in overlay mode. "Hovered" is the viewport's hover, not the thumb's: the input controller
  sends `PointerEnter` to every node in the hover chain, so a pointer anywhere inside the viewport
  reaches `handle_hover_transitions`, which is also the state the thumb colour already reads.
- `paint_scrollbar()` paints the track as the thumb column and keeps the capsule thumb
  (`draw_rounded_rect`, radius `extent * 0.5`), which already rounds the ends.
- `dispatch_event()` invalidates when a thumb drag starts or ends, because the width changes and
  the narrower strip has to erase its fringe.

Files: `albor/widgets/scroll_view.h`, `albor/widgets/scroll_view.cpp`.
Tests (`tests/scroll_view.test.cpp`): a macOS-preset view reserves no gutter and the content
viewport equals the bounds; an `as_needed` overlay bar is drawn while overflowing and the gutter
path is untouched for a derived theme; the thumb is `scrollbar_extent` wide at rest and
`scrollbar_hover_extent` wide on enter, back at rest on leave, and wider while dragging.

`tests/list_box.test.cpp`: `TheSelectionBandStopsAtTheBarGutter` builds a macOS theme and expects a
310px band. Under the overlay rule the band is 320px, which is native. Update it to assert the
overlay case and add a gutter case with `ThemeBuilder::light` so §8.2 keeps its coverage.

### P4 — vertical tab selection row

`albor/widgets/tab_view.cpp`: a vertical strip paints the selection as a row when the installed
theme fills `tab_selection_plate`. The row is the header inset by `tab_row_inset`, rounded by
`tab_row_radius`, drawn before the title, and it suppresses the 2px marker. A top strip keeps the
indicator, and a plain `Theme` or a preset that fills nothing keeps both the indicator and every
existing rectangle, so the current tab tests stay green.

`albor/widgets/tab_view.h`: the two private helpers and their comments.
Files: `albor/style/theme.h` (already in P2), `albor/widgets/tab_view.h`,
`albor/widgets/tab_view.cpp`, `albor/style/theme.cpp` (the macOS row tokens).
Tests (`tests/widgets.test.cpp`): a macOS vertical strip records one rounded accent row and no
indicator rect; a macOS horizontal strip still records the indicator; a theme with a transparent
selection plate records the indicator even when vertical; the row respects the inset and radius;
an unselected row keeps `tab_label`.

### P5 — docs, spec and ledger

- `albor-design-v4.md` §8.4: record the macOS surface rules (cards, wells, overlay bars, default
  buttons, sidebar rows), the "transparent role is silent" gate, and the new statement that the
  dialog's default control carries a plate role of its own rather than being a key convention.
- `docs/concepts/theming.md`: what a preset now decides beyond metrics.
- `docs/components/panel.md`, `button.md`, `tab-view.md`, `scroll-view.md`: replace the statement
  that a bar always occupies a gutter, document the default-button role, the sidebar row and the
  card face.
- `macos-windows.md`: one line recording that the preset work is portable, that `fluent` keeps its
  gutter while gaining its well and primary control, and that the derived baseline gains its well.
- `bash build/check-format.sh` (its sweep covers every changed file), and the comment sweep in the
  same pass.

## Acceptance criteria, mapped

| criterion | covered by |
| --- | --- |
| a dialog of `Panel`/`TextField`/`ComboBox`/`Button`/`Scrollbar` reads native on all platforms with no authored values | P1–P4; `ComboBox` already resolves `text_field_plate`/`text_field_border`, so the field split reaches it |
| the macOS default button is distinct from Cancel | P2 |
| the macOS scrollbar overlays instead of reserving a gutter | P3 |
| a vertical `TabView` renders an accent row without the leftover 2px bar | P4 |
| preset tests assert the new macOS tokens and lock the fluent and derived native values | P1–P4 tests, plus the preset tests that assert each platform's card, well, gutter, default plate and row role |

## Verification

```text
bash build/snapshot.sh issue-201-start
cmake --preset debug
cmake --build --preset debug
bash build/check-format.sh
./.build-debug/tests/albor-tests
```

The build and the format sweep run in every phase. The suite runs once at the end; under the agent
contract it runs only when the owner asks, and CI runs it for the PR.

## Risks

- **Overlay changes layout.** Every macOS `ScrollView` gains 7px of content width. That is the
  point, but it is the only behavioural (not merely visual) change here, and a test that pins a
  content width under the macOS preset will need the same correction as the `ListBox` band test.
- **A white field on a white card** is legible only through the hairline. If the hairline is ever
  dropped, the field disappears; the test on the field line is load-bearing, not cosmetic.
- **`Panel` inside `Panel`** becomes flat in light mode. Native settings pages nest plates rarely;
  if Xyce Studio does, the inner card needs an authored face, which the issue explicitly wants to
  avoid. Worth checking against the studio before P1 lands.
- **No timed fade.** Rest translucency plus a hover alpha is native enough, but a true fade needs
  an animation hook in `ScrollView` (§7.3). Out of scope.
- **A composed `TabTitle`** keeps its own ink: the row plate paints behind it, but a `Label` inside
  the title resolves `label_foreground`, not `tab_selection_label`. A plain `Tab { label }` is
  fully themed. See the open decision below.

## Open decisions

1. **Composed tab titles — resolved.** The plain-label path ships; a composed title draws its own
   ink, which `docs/components/tab-view.md` and the design doc now state.
2. **Dark-mode lift values — resolved.** Card `+0.10`, field `+0.04` on macOS, button `+0.16`; the
   fluent and derived wells lift `+0.10`, because their cards lift less than the macOS one.
3. **Fluent wells and primary button — resolved in this change.** Both presets are updated, and the
   preset tests lock the new values.
4. **Per-instance overlay override — rejected.** The preset decides the bar, as it decides height
   and padding.

## Not in scope

- A `ThemeController` mutation API. The issue names the preset as the owner, and a mutable theme
  would break the borrowed-pointer contract `sync_theme` maintains.
- Timed scrollbar fade and auto-hide.
- An unselected sidebar row hover plate: the row role covers the selection only, which is what the
  issue asks for. A hover tint would need a sixth palette role and is not part of the acceptance.
- Nested-card differentiation, card shadows beyond the existing authored `drop_shadow`.
- `.ui`, codegen, manifest, semantic-tree or automation changes: no property is added.
