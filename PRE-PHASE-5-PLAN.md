# PRE-PHASE-5-PLAN.md

Authoring status
Author reviewed, 2026-10-07
Source: PRE-PHASE-5-PLAN.md

## Status
Agreed. This document governs the work between the completion of Phase 4
(commit 4785599) and the start of Phase 5. Phase 5 is deferred until the
platform can render a real application.

## Deferred: Phase 5

Phase 5 (devrt, automation, language server, standalone preview host,
accessibility adapters, inspector, windows/linux platform adapters) is
deferred until the application-readiness line below lands. The platform
must be able to render a real application — widgets that are really used,
correct rendering, non-scary examples — before developer tooling is worth
building on top of it.

The deferral is recorded here; BACKLOG.md points back at this document.

## Why this order

The framework currently cannot render a real application correctly, and
RENDERING-DEFECTS.md documents why: text draws at the wrong point size with
unshaped bytes (D1), clips are discarded (D2), paint order collapses across
primitive categories (D3), rounded corners are dropped (D4), widgets never
render children (D5), interaction states beyond the disabled fade do not
exist and no style module does exist (D7), transparent paints are recorded
as real geometry (D8).

Real widgets on top of these defects render broken. So the order is fixed:
fidelity first, then feel, then facade, then widgets, then examples.

## Decided

- Popup surface: WindowRole::Popup gets real macOS support (borderless
  style mask, floating window level, no activate on show, click outside
  dismissal). ComboBox drops its list into a popup window; tooltips and
  context menus become possible later without rework.
- Widget set for this cycle: Panel/Card, TextField, ScrollView, ListBox,
  ComboBox, ProgressBar, TabView, Image. Menus and toolbar are follow-up
  work on top of the popup surface.
- Theme scope: dark by default with a light palette that can be flipped at
  runtime, and one accent colour.
- TextField depth: full featured — caret with blink, selection, editing
  state machine, clipboard through a portable Clipboard surface with a
  macOS pasteboard adapter, IME through a real NSTextInputClient
  conformance (marked text, candidate window anchoring), undo/redo.

## Cluster 1 (C1): rendering fidelity

Executes Phases A and B of RENDERING-DEFECTS.md. Clears D1, D2, D3, D4,
D8, D9.2, D9.3 and the allocation churn of D9.5. Does not touch the two
stage threading (AD-18).

1. Define one scene command type as a tagged union over the primitive
   kinds RenderContext already declares: rect, rounded rect, text, line
   batch, path, picture, image. A command carries its clip rect,
   transform, paint or stroke style, and geometry payload; retained
   resources cross as shared_ptr<const> handles (§6.4, AD-06).
2. FrameScene becomes the window metrics, the background colour and one
   vector of commands in paint order. The parallel vectors go away.
3. The recorder records in paint order, keeps its clip and transform
   stack, and culls at record time: empty geometry and paints that
   neither fill nor stroke are dropped (D8).
4. The backend replay becomes one implementation in execute_frame: walk
   the commands in order, apply the clip as a scissor when a command's
   clip differs from the previous one, draw each command through the
   render context (D2, D3).
5. PictureResource adopts the same command type, so retained pictures and
   the frame share one command vocabulary and one replay path (D9.2,
   D9.3).
6. The recorder retains its buffers across frames and reuses capacity, so
   a steady state frame reallocates nothing (D9.5).
7. TextLayout becomes a shared_ptr<const> shaped run: glyph identifiers,
   glyph positions, and one baseline origin per line for wrapped runs.
   The scalar size, ascent and descent stay; the UTF-8 string as the draw
   payload goes away (D1).
8. The text engine returns the shaped result from measure, and the draw
   path replays it verbatim, so measurement and drawing consume one
   representation (D1).
9. The engine caches shaped runs keyed by text, font, size and maximum
   width, so a steady state frame pays a lookup per run (D6 text cost).
10. The glyph atlas is owned by the text engine; the renderer binds it as
    a texture and draws glyph quads. No text type names a renderer type
    and no renderer type names a text engine type (§3.5, AD-19).

Verification (§27, from RENDERING-DEFECTS.md): paint order is preserved
across categories; a recorded clip reaches stage two; a rounded radius
survives recording; an alpha zero paint is culled; a wrapped run draws
with the shaped glyphs at the measured size.

## Cluster 2 (C2): style and interaction states

Clears D7 and D5. A tree of widgets now looks and behaves like controls.

1. New style/ module: Theme carries the dark and light palettes, the
   accent colour, the font scale, the control radius and the control
   metrics. Widgets consult the theme through one surface; hardcoded
   colour literals leave the widget headers.
2. The palette flips at runtime, which invalidates every visible frame.
3. Pointer Enter and Leave events join input/ (§8.3), so a hover state
   can exist.
4. The existing controls (Button, Label, Slider, CheckBox, Canvas) gain
   hover, pressed and focus visuals driven by the theme.
5. Every widget chains to the component render path, so children and the
   component background render (D5); Component::paint_background and
   Component::background stop being dead code.

## Cluster 3 (C3): application facade

The composition root a developer writes must fit on one screen.

1. AppHost::run owns the wiring the examples currently repeat: platform
   pack, Skia backend, text engine, window and surface, pipeline and
   executor, input routing, frame, resize and close handlers.
2. The raw APIs stay public and documented as the escape hatch for
   applications that need the pieces separately.
3. The facade carries its own unit tests and holds the coverage floor.

## Cluster 4 (C4): the widget set

Built on C1 and C2. Each widget ships with semantic tests (§10.1: role,
name, states, actions — a widget is not done without them), layout tests,
a compiler catalog entry so .ui can declare it, and coverage at or above
the floor.

Tier 1:
1. Panel/Card — themed container with radius and padding.
2. ScrollView — needs the C1 clips; wheel handling and scrollbar state.
3. TextField — full featured per the decided depth; the caret blinks
   through the animation core (§7.3).
4. ListBox — built on ScrollView; selection model; keyboard navigation.

Tier 2:
5. WindowRole::Popup on macOS, which the ComboBox consumes.
6. ComboBox — trigger plus a popup ListBox.
7. ProgressBar, TabView, Image.

## Cluster 5 (C5): the examples

The examples must show the framework, not scare the developer reading
main.cpp.

1. A new Showcase application: themed sections that exercise every
   control, a theme toggle, .ui layout with generated facades and C++
   handlers wired through the facade slots.
2. hello-window drops to roughly twenty lines through the application
   facade.
3. greeting stays the .ui compilation demo.
4. chart migrates onto the ordered display list and keeps its custom
   painting story.

## Cluster 6 (C6): final gates and push

The full unit suite, check-format.sh, the brace and comment scans, the
coverage floor gate, check-link-boundary and check-build-scope; a
snapshot; a commit; a push.

## Risks

- C1 changes render/ and ui/scene_builder deeper than any change so far;
  the diff is large but the §27 verification list already exists, so the
  cluster lands as one reviewed unit.
- TextField IME touches the macOS view delegate, which is fiddly; the
  insertText: path exists (platform_window.macos.mm:186), the remaining
  work is the marked text contract and the candidate anchor.
- Popup key window behaviour has macOS quirks; a small spike before the
  widget work on it.
- Every module added must hold the 95% coverage floor that
  check-coverage.sh now enforces.
