# Xyce Studio application readiness — Albor gap analysis

**Date:** 2026-10-26
**Status:** tracked guidance — the working plan for the next implementation cycles
**Reference application:** `/Users/rogelio/Projects/xyce-studio` (Slint UI, 23 `.slint` files,
about 9,100 lines)
**Purpose:** state what the reference application needs that Albor does not yet provide, and rank
the remaining gaps by application impact rather than by backlog order.

This revision replaces the 2026-10-10 snapshot. Most of the blockers in that snapshot have since
shipped. What remains is a much smaller list of concrete controls, one language feature, and one
data-lifetime defect.

## Verdict

Albor can build a substantial vertical slice today: forms, tabs, lists, scroll views, images,
progress, radio groups, numeric steppers, command-bound buttons and a retained custom chart. It
cannot yet host the full Xyce Studio shell without either duplicating component structure or
living without controls the application uses on nearly every page.

**There are four open P0 gaps.**

1. ComboBox — #18
2. Panel/Card — #15
3. Icon/content Button — no ticket
4. Styled/syntax-highlighted TextField — no ticket

Cross-file `.ui` composition and the TextField undo lifetime defect are implemented on the
`application-readiness` branch and await review. The remaining four are composition and control
vocabulary for the shell, the toolbar and the netlist editor.

A full port is not the right next step. The right next step is the vertical slice described at the
end of this document, with the open P0 list cleared first.

## What changed since the last revision

The 2026-10-10 analysis listed a much larger deficit. These items have shipped and are no longer
part of the gap.

| 2026-10-10 blocker | Current state |
| --- | --- |
| No text input at all | `TextField` ships with caret, selection, clipboard, IME, undo, multi-line and its own scrolling (#13, #67) |
| No conditional element or `visible` binding | `if`, `visible` and `Repeat` ship (#39) |
| No `struct`, `enum`, `global` or `function` | All four are top-level declarations, with records and value sets (`docs/ui-language/types.md`) |
| No `@children` slot or component reuse | Same-file composition, one-level inheritance with `@children`, and cross-file `import` ship on this branch |
| No scroll container | `ScrollView` and virtualized `ListBox` ship (#14, #16) |
| Missing RadioButton, RadioGroup, SpinBox, Image, ProgressBar, TabView | Radio controls, `NumericStepper`, `Image`, `ProgressBar`, `TabView`, `Tab` and `TabTitle` ship (#19) |
| No image loading or compile-time asset embedding | `@image` embeds PNG assets; preview and devrt resolve the same path (#9, #19) |
| No commands in `.ui` | Command objects and `action:` binding on Button, Slider and CheckBox ship (#2); a component-level `command` declaration remains #56 |
| No focus state | `ControlState::Focused` and the focus ring ship (#23); the headless proof remains #74 |
| No per-node styling | Runtime opacity, border and drop shadow ship (#23); the `.ui` surface remains #72 |
| Two-way binding ergonomics | The explicit write-back pattern remains; it is workable and no longer the first blocker |
| No draw vocabulary | Still two primitives; #28 remains |
| No menus or context menus | Still absent; #55 remains |
| No ComboBox, Panel/Card | Still absent; #18 and #15 remain |
| TextField polish | Word selection (#68) and IME mark wrapping (#70) remain; undo lifetime (#69) is fixed on this branch |

The result is a framework that covers the ordinary form and list surface well. The remaining work
is concentrated in the application shell, reusable component structure, and the two heavyweight
editing surfaces.

## Current baseline

- **Widget catalogue:** 21 elements — eleven containers and ten leaves
  (`docs/components/index.md`).
- **Composition:** same-file composition, cross-file `import "path.ui"` and one-level inheritance.
  Each imported file declares one component named after its stem, imports resolve relative to the
  importing file, and cycles are rejected. Compiled output constructs the imported facade; preview
  and the dev runtime expand the imported tree (`docs/ui-language/components.md`).
- **Text editing:** one `TextField` leaf, single-run rendering, with caret, selection, clipboard,
  IME, undo, multi-line and scrolling (`widgets/text_field.h`). The field owns its undo entries
  and revokes them when it is destroyed or bound to another stack.
- **Button:** text-only leaf with a plate, label, disabled state, command binding and
  accessibility action (`widgets/button.h`).
- **Canvas:** a closed draw vocabulary of `fill_rect` and `fill_rounded_rect`; richer content
  belongs in a manifest-declared native custom render node (`docs/components/canvas.md`).
- **Paths:** `PathVerb` is `MoveTo`, `LineTo` and `Close`; no curve verbs
  (`render/path_resource.h`).
- **Accessibility:** macOS NSAccessibility and Windows UI Automation bridges, with roles and
  actions; increment, decrement and toggle are not yet reachable, and value writing is read-only
  (`macos-windows.md`).
- **Platforms:** macOS and Windows are implemented; Linux is planned. The current branch has not
  been pushed, so its Windows CI run is pending.

## P0 — blockers for an application port

### P0.1 Cross-file `.ui` composition — done on `application-readiness`

**Ticket:** none; residual of #39.

**State.** Implemented on this branch. `import "path.ui";` resolves relative to the importing
file, an imported file declares exactly one component named after its stem, imports may be
transitive, and cycles are rejected. The compiled path generates a facade per file and constructs
it from the importer. The preview and the dev runtime expand the imported tree because they have
no generated facade; expression substitution applies the instance properties and the declared
defaults. CMake `IMPORTS` records the direct imports, orders the generated units, and the build
scope proof covers imported body and interface edits.

**Acceptance evidence.**

- Two `.ui` files compile, preview and reload through the compiled and design-time paths.
- Cycle, missing-file and naming diagnostics carry the offending file path.
- Editing an imported body rebuilds only that component; editing its interface rebuilds it and its
  importer (`bash build/check-build-scope.sh`).
- Same-file composition and inheritance tests still compile and pass review.

The feature awaits the phase review and the Windows CI run before it is called shipped.

### P0.2 ComboBox

**Ticket:** #18.

**Why it blocks.** The reference application has 30 ComboBox instantiations. There is no trigger,
no popup list, no expanded state and no single-choice selection surface for a compact set of
options. `ListBox` and `WindowRole::Popup` now exist, so the two hard dependencies are in place.

**Next move.** Build the trigger and popup around `ListBox`, using the popup surface for the drop
down. Add open/close, pointer and keyboard selection, and the semantic states a screen reader
needs: expanded, selected and disabled.

**Acceptance.**

- A ComboBox opens on click and on the keyboard, selects with arrows and Enter, and dismisses on
  Escape or an outside click.
- The selected item is visible on the trigger.
- The `.ui` catalogue exposes the element and its options.
- Semantics expose role, name, expanded, selected and the choose action.
- Layout, input, semantic and dual-engine tests exist.

### P0.3 Panel/Card

**Ticket:** #15.

**Why it blocks.** Every settings page and the shell are built from themed sections. A `Padding`
around a `Canvas` can imitate the pixels, but it does not give the application a reusable container
with a background, radius, padding and group semantics. Without it, every page reimplements the
same surface and the same accessibility grouping.

**Next move.** Add a container that owns its background, border radius, border and padding, exposes
a group semantic, and participates in layout like the other containers. Keep the first version
small: no title bar, no collapse behaviour, no per-node style cascade.

**Acceptance.**

- A `.ui` form declares a panel and arranges its children inside the padding.
- The panel paints its background, radius and border through the theme.
- It announces a group role and its author `id`.
- Layout and semantic tests cover the container, and the compiler catalogue exposes it.

### P0.4 TextField undo lifetime — done on `application-readiness`

**Ticket:** #69.

**State.** Implemented on this branch. `UndoEntry` carries an owner ticket and a live flag.
`UndoStack::remove_owner` revokes the ticket before filtering, drops dead folded groups and skips
dead steps inside a mixed group. A `TextField` owns one ticket, records its edits under it, and
revokes the entries when it is destroyed or rebound to another stack. The stack reports only live
steps through `can_undo`, `can_redo`, `depth` and `top_label`.

**Acceptance evidence.**

- A destroyed field leaves no entry that undo can call.
- Rebinding a field to another stack revokes its entries on the old stack.
- A folded group with no live step disappears; a mixed group keeps its live steps.
- Both stack directions have regressions, and the header states the contract.

The tests compile but have not been executed in this phase; the branch awaits review and CI.

### P0.5 Icon/content Button

**Ticket:** none.

**Why it blocks.** `Button` is a text-only leaf. The application needs icon buttons, icon-plus-label
buttons and buttons whose content is composed rather than a single caption. The current workaround
is to paint a custom control in C++, which loses the command binding and the standard button
semantics.

**Next move.** Decide between an `icon`/`image` property plus an optional label, and a content slot
that can hold an icon and a run. Start with the property if the application only needs icon and
icon-plus-label buttons; add the slot only if truly composed content is required. In either case,
keep the text-only path byte-compatible.

**Acceptance.**

- An icon-only button is measurable, paintable and has an accessible name.
- An icon-plus-label button lays the two out without clipping and keeps the existing label
  measurement.
- A button with no icon renders exactly as it does today.
- The compiler catalogue, codegen and semantic tests cover the new properties.

### P0.6 Styled/syntax-highlighted TextField

**Ticket:** none.

**Why it blocks.** The netlist editor is the application's core editing surface. `TextField`
paints one run; it has no per-range colour, underline or style. Syntax highlighting cannot be
layered on top without duplicating shaping, because the caret and the overlay drift unless both
use the same shaped run and the same scroll offset.

**Next move.** Define a decoration or styled-run API that shares the field's `TextLayout`, caret
metrics and scroll. The field remains one owner of shaping; decorations are ranges applied to the
runs it already measured. Do not introduce a second text engine or a parallel layout path.

**Acceptance.**

- A netlist fixture highlights keywords, numbers and comments with distinct colours.
- Diagnostics can underline a byte range.
- Caret, selection and highlight stay aligned after wrapping and scrolling.
- A single-run field with no decorations renders exactly as it does today.
- The API is reachable from C++ and, for a declared range list, from `.ui`.

This is P0 for the netlist editor. The rest of the shell can be ported before it lands.

## P1 — important, but a vertical slice can proceed without them

| Gap | Why it is P1 | Ticket | Next move |
| --- | --- | --- | --- |
| Menus and context menus | The shell needs a menu bar and right-click actions, but a slice can use buttons and a panel toolbar. | #55 | Model menu items over `Command`; decide native bar versus in-tree bar; add keyboard traversal and semantic actions. |
| Declarative draw vocabulary and path curves | Charts can use a retained native custom render node today. Declarative content still cannot draw text, lines, paths or images. | #28 | Add quadratic and cubic path verbs, more draw primitives and a measure operation; settle Option B/C for expensive content. |
| Per-node `.ui` styling | The runtime already carries opacity, border and drop shadow. This is visual fidelity, not application function. | #72 | Add the catalogue properties and codegen mappings; the record-type blocker is gone. |
| Focus-ring proof | The ring ships and is themed; it has no headless pixel proof or semantic focus test. | #74 | Raster a focused and unfocused button, assert the ring at the configured offset and width, and assert the semantic focus is observable. |
| Accessibility actions | A screen reader can activate but cannot move a slider or toggle a checkbox; stepper increment/decrement are also missing on Windows. | #57 | Add one role-to-action table in portable code and implement the platform methods from it. |
| TextField double-click and IME wrapping | Editor quality, not a blocker for the first slice. | #68, #70 | Put word boundaries in the text engine; reuse the measured wrap ceiling for the IME mark. |
| Command ergonomics | Applications can pass commands from C++ today; undo/redo declaration and `.ui` command declarations are rough edges. | #58, #56 | Add named `declare_undo`/`declare_redo` and a component-level command declaration with `action:` consumers. |

## Not blockers — do not pull these into the application cycle

| Area | Issues | Why it can wait |
| --- | --- | --- |
| Conformance and tooling | #8, #25, #6, #7 | The automation compatibility, Python harness, LSP navigation and manifest consistency suites are important for framework confidence, not for porting this application. |
| Showcase and release ritual | #20, #40 | A showcase and the final gate are downstream of the application slice. |
| Rendering and performance backlog | #24, #32–#36, #107, #112–#118 | The current renderer is fast enough to port against; these are steady-state cost, damage-rectangle and text fast-path work. |
| RTL and bidi | #10, #98, #99 | The reference application is English/LTR; RTL can be a separate correctness cycle. |
| Linux and AT-SPI | #41, #42, #110 | macOS and Windows are the implemented platforms. #41 and #42 are stale descriptions and need re-scoping before they can be worked. |
| Renderer roadmap and retirement | #102, #109, #111 | `render-albor` works on both native platforms; retiring Skia and broadening the backend roadmap is parallel infrastructure work. |
| Product and documentation decisions | #26, #27, #47, #48, #119 | Important, but none of these stops a form from rendering or editing. |

## Issue closure for the current branch

The branch `application-readiness` completes **#69** — the TextField undo lifetime. The branch has
no ticket for cross-file `.ui` composition; that work is tracked in this document. The PR should
carry exactly one closing keyword once the phase review and CI pass:

```text
Closes #69
```

Do not add closing keywords for the following without separate verification. They are partial,
stale or adjacent work that this branch does not finish.

- **#15, #18, #55, #28, #72, #74, #57, #68, #70, #58, #56** — still open by definition; the branch
  either does not touch them or only touches their surrounding area.
- **#41** — the description says Win32 is not started, but the Windows platform and Direct3D
  backend now exist. The issue is stale and should be re-scoped, not closed as completed by this
  branch.
- **#42** — the description says UI Automation does not exist, but the Windows bridge is
  implemented and tested. It is stale in the same way; re-scope it separately.
- **#109** — the D3D backend is functional and covered by Windows CI, but the issue is an epic
  acceptance item with parity and coverage language that needs an explicit sign-off. Verify it
  against its own acceptance criteria before closing.

Closing only #69 is the correct behavior for this merge. The stale tickets should be corrected in
a separate issue-hygiene pass so their status stops misleading the next cycle.

## Recommended sequence

| Cycle | Goal | Work |
| --- | --- | --- |
| 1 — make components reusable | A page can be assembled from shared `.ui` files | Cross-file composition (done, awaiting review), #15 Panel/Card, #18 ComboBox, #69 undo lifetime (done, awaiting review), icon/content Button |
| 2 — make the editing surfaces real | The netlist editor and chart views reach feature parity | Styled TextField, #28 path curves and draw primitives |
| 3 — make the shell complete | The application looks and behaves like the reference | #55 menus, #72 per-node styling, #74 focus proof, #57 accessibility actions, #68/#70 field polish, #58/#56 command ergonomics |

The order inside cycle 1 is deliberate: cross-file composition first, because every other widget
is reused through it; undo lifetime second, because it is a correctness defect; then the remaining
controls in the order the shell needs them.

## First vertical slice

The first application slice should be small enough to finish in one cycle and broad enough to
exercise every layer that the full port will depend on.

- A window with a `TabView` of two pages, each page a shared `Panel` imported from another `.ui`
  file.
- One page with a form: `TextField`, `ComboBox`, `NumericStepper`, `RadioGroup`, `CheckBox`,
  `Slider`, `ProgressBar` and `Image`.
- One page with a `ScrollView` + `ListBox` table and a chart drawn by a retained custom render
  node.
- A close/reopen action on the form, followed by undo on the application stack, to prove the undo
  lifetime fix in a real flow.
- A toolbar built from icon buttons; menus and per-node styling can wait for cycle 3.

This slice is the acceptance test for cycle 1. It should run on both macOS and Windows before the
netlist editor work starts.

## Definition of done for the application POC

The framework is ready for the full port when all of the following are true:

- A window can be assembled from multiple `.ui` files with shared panel and page components.
- A settings page can use a Panel/Card, ComboBox, TextField, NumericStepper, RadioGroup,
  CheckBox, Slider, ProgressBar, Image, ScrollView and ListBox without a C++ fallback.
- The netlist editor supports syntax highlighting, diagnostics, selection, clipboard, IME, undo
  and scroll, with caret and highlights aligned.
- Charts are drawn through a retained custom render node; the render execution path never calls
  application code.
- A form can be closed and rebuilt, and undo on the application stack remains safe.
- Menus and context menus are reachable, and assistive technology can activate, toggle,
  increment and decrement the controls that support those actions.
- The slice builds and runs on macOS and Windows, and the Windows CI run is green.
- No P0 item in this document remains open. Every remaining P1 item has either shipped or is
  explicitly deferred with a documented workaround.

## Validation baseline

- Branch: `application-readiness`, based on `main` at `b0c0277`.
- The local macOS debug configure and build pass after the cross-file and undo changes.
- `bash build/check-build-scope.sh` passes for root, imported-body and imported-interface edits.
- Unit suites compile but have not been executed in this phase, per the repository workflow.
- Windows build and parity counts are authoritative in CI; this host cannot build them, and the
  branch has not been pushed yet.
- `bash build/check-format.sh` is run at the phase boundary before the review.
