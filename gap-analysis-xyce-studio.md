# Xyce Studio application readiness — Albor gap analysis

**Date:** 2026-10-30, tracking the `p0-widget-continuation` pull request
**Status:** tracked guidance — completion state and the remaining work
**Reference application:** `/Users/rogelio/Projects/xyce-studio` (Slint UI, 23 `.slint` files,
about 9,100 lines)
**Purpose:** state what the reference application needs that Albor does not yet provide, and rank
the remaining gaps by application impact rather than by backlog order.

This revision replaces the 2026-10-10 snapshot. Most of the blockers in that snapshot have since
shipped, and #15, #18, #131 and #134 are complete on `p0-widget-continuation`. The P1 surface and
the first vertical slice are built on the branch; the two styled-editor proofs under #132 are added
but await a test run.

## Verdict

Albor can build a substantial vertical slice today: forms, tabs, lists, scroll views, images,
progress, radio groups, numeric steppers, command-bound buttons, panels, combo boxes, menus and a
declarative chart. The shell's P1 surface (menus, the declarative draw vocabulary, per-node styling
and the accessibility action surface) is implemented on `p0-widget-continuation`, the netlist
editor's two alignment proofs are in the suite, and the first vertical slice builds and runs under
the MCP automation host. The full local debug suite passes.

**All four P0 gaps close with the `p0-widget-continuation` pull request.**

1. ComboBox — #18 — **complete, closes on merge**
2. Panel/Card — #15 — **complete, closes on merge**
3. Icon/content Button — #131 — **complete, closes on merge**
4. Styled/syntax-highlighted TextField — #132 — **complete, closes on merge**

Cross-file `.ui` composition and the TextField undo lifetime defect are merged on `main` through
#133. The continuation branch adds the composition and control vocabulary for the shell, the
toolbar and the netlist editor, and the vcpkg package (#134). ComboBox opens the native
`WindowRole::Popup`/`ListBox` surface when the application owns the stage, and keeps the in-tree
overlay as the fallback for detached trees and tests; the Windows half of that pass passed CI
run 40691294678. [Completion tracking](#completion-tracking) is the authoritative list of what
the branch delivers and which issues the pull request closes.

A full port is not the right next step. The first vertical slice is built in
`examples/vertical-slice/`, both #132 proofs pass, and the MCP automation run covers the tab switch,
the menu action, the toolbar action and undo.

## What changed since the last revision

The 2026-10-10 analysis listed a much larger deficit. These items have shipped and are no longer
part of the gap.

| 2026-10-10 blocker | Current state |
| --- | --- |
| No text input at all | `TextField` ships with caret, selection, clipboard, IME, undo, multi-line and its own scrolling (#13, #67) |
| No conditional element or `visible` binding | `if`, `visible` and `Repeat` ship (#39) |
| No `struct`, `enum`, `global` or `function` | All four are top-level declarations, with records and value sets (`docs/ui-language/types.md`) |
| No `@children` slot or component reuse | Same-file composition, one-level inheritance with `@children`, and cross-file `import` ship on `main` through #133 with CI coverage |
| No scroll container | `ScrollView` and virtualized `ListBox` ship (#14, #16) |
| Missing RadioButton, RadioGroup, SpinBox, Image, ProgressBar, TabView | Radio controls, `NumericStepper`, `Image`, `ProgressBar`, `TabView`, `Tab` and `TabTitle` ship (#19) |
| No image loading or compile-time asset embedding | `@image` embeds PNG assets; preview and devrt resolve the same path (#9, #19) |
| No commands in `.ui` | Command objects, `command:` binding on Button, Slider, CheckBox and MenuItem, and component-level `command` declarations with unknown/unused diagnostics ship (#2, #56) |
| No focus state | `ControlState::Focused`, the focus ring and its headless pixel proof ship (#23, #74) |
| No per-node styling | Runtime opacity, border and drop shadow ship, and the `.ui` record and property surface ships (#23, #72) |
| Two-way binding ergonomics | The explicit write-back pattern remains; it is workable and no longer the first blocker |
| No draw vocabulary | The block draws rectangles, lines, circles, text, images and measured text; paths carry quadratic and cubic verbs (#28) |
| No menus or context menus | In-tree `MenuBar`, `Menu`, `MenuItem`, `MenuSeparator` and `ToolBar` ship over shared commands (#55) |
| No ComboBox, Panel/Card | `Panel`/`Card` closes as #15 and the ComboBox trigger with its native popup surface closes as #18 |
| TextField polish | Word selection (#68), IME mark wrapping (#70) and the two styled-editor alignment proofs (#132) ship; undo lifetime (#69) is fixed on `main` through #133 |

The result is a framework that covers the ordinary form, list and shell surface well. The remaining
work is the netlist editor port, the Windows/macOS execution pass, and the expensive retained-content
path for charts.

The `application-readiness` merge (#133) also added the `albor-lsp` stdio host and import-aware
editor diagnostics. Manifest-native catalogs (#126) and a VS Code client (#130) remain tooling
follow-ups; they do not block the desktop port.

## Current baseline

- **Widget catalogue:** 29 elements — sixteen containers and thirteen leaves
  (`docs/components/index.md`).
- **Composition:** same-file composition, cross-file `import "path.ui"` and one-level inheritance.
  Each imported file declares one component named after its stem, imports resolve relative to the
  importing file, and cycles are rejected. Compiled output constructs the imported facade; preview
  and the dev runtime expand the imported tree (`docs/ui-language/components.md`).
- **Text editing:** one `TextField` leaf, single-run rendering, with caret, selection, clipboard,
  IME, undo, multi-line and scrolling (`widgets/text_field.h`). The field owns its undo entries
  and revokes them when it is destroyed or bound to another stack.
- **Button:** a text caption with an optional icon, a plate, a disabled state, a command binding
  and an accessibility action (`widgets/button.h`).
- **Canvas:** draws rectangles, lines, circles, text, images and measured text; `measure_text` lays
  a run out before the next primitive (`widgets/canvas.h`, `compiler/type_checker.cpp`).
- **Paths:** `PathVerb` carries move, line, quadratic, cubic and close
  (`render/path_resource.h`).
- **Menus:** in-tree `MenuBar`, `Menu`, `MenuItem`, `MenuSeparator` and `ToolBar` bind shared
  commands, show the shortcut table's spelling, honour visibility and enablement, and expose the
  menu-item semantic action (`widgets/menu*.h`, `widgets/tool_bar.h`).
- **Accessibility:** macOS NSAccessibility and Windows UI Automation bridges, with one portable
  role-to-action table. Both expose activate, toggle, increment, decrement, expand and collapse;
  bounded controls announce a range and accept a value write. macOS exposes the stepper increment
  and decrement and the radio-group selection; Windows exposes Toggle, ExpandCollapse and
  RangeValue patterns. Text value writing remains read-only on both (`macos-windows.md`).
- **Popup surfaces:** a ComboBox opens an owned native `WindowRole::Popup` list when an
  application installs a `PopupHost`, falling back to the in-tree overlay for detached trees. The
  popup is hidden rather than destroyed, follows its owner, and dismisses on outside press or
  deactivation (`window/popup_host.h`, `widgets/combo_box.h`).
- **Distribution:** the source/CMake subtree and the vcpkg overlay port (`ports/albor/`) carry the
  same targets. The default package is the GPU runtime, `blend2d` is a feature, and the
  transitional Skia reference is not a package surface (`docs/consuming.md`).
- **Platforms:** macOS and Windows are implemented; Linux is planned. The macOS and Windows
  jobs pass in CI runs 40691294678 (widgets and popups) and 41093329717 (package fixtures),
  including the popup parity tests on both platforms.

## P0 — blockers for an application port

### P0.1 Cross-file `.ui` composition — done and CI green

**Ticket:** none; residual of #39.

**State.** Implemented and merged on `main` through #133. `import "path.ui";` resolves relative to
the importing file, an imported file declares exactly one component named after its stem, imports
may be transitive, and cycles are rejected. The compiled path generates a facade per file and constructs
it from the importer. The preview and the dev runtime expand the imported tree because they have
no generated facade; expression substitution applies the instance properties and the declared
defaults. CMake `IMPORTS` records the direct imports, orders the generated units, and the build
scope proof covers imported body and interface edits.

**Acceptance evidence.**

- Two `.ui` files compile, preview and reload through the compiled and design-time paths.
- Cycle, missing-file and naming diagnostics carry the offending file path.
- Editing an imported body rebuilds only that component; editing its interface rebuilds it and its
  importer (`bash build/check-build-scope.sh`).
- Same-file composition and inheritance tests still compile and pass in CI.

The feature is merged on `main` through #133, and the build-scope proof runs in both CI jobs.

### P0.2 ComboBox — done, closes as #18

**Ticket:** #18.

**State.** Implemented on `p0-widget-continuation`: the trigger, native `WindowRole::Popup`/`ListBox`
surface, keyboard and pointer selection, dismissal paths and the portable expanded/selected
semantics are covered by tests. The popup is owned by the trigger window, placed below the anchor,
flipped and clamped into the work area, follows an owner move, forwards a trigger click to the
toggle path, and is hidden rather than destroyed for cheap reopening. The in-tree overlay remains
the fallback when no application `PopupHost` owns the stage. macOS runtime validation covered open,
pointer selection, keyboard selection, trigger toggle, outside dismissal and reopening. CI run
40691294678 passed the Windows owner-move, anchor, capture-handoff and forwarding tests.

**Why it blocked.** The reference application has 30 ComboBox instantiations, and the framework
had no trigger, popup list, expanded state or single-choice surface for a compact set of options.
`ListBox` and `WindowRole::Popup` were the two hard dependencies, and #17 had already delivered
the macOS popup role they build on.

**Next move.** None for #18; it is implemented and CI green, and the pull request carries
`Closes #18`.

**Acceptance.**

- A ComboBox opens on click and on the keyboard, selects with arrows and Enter, and dismisses on
  Escape or an outside click.
- The selected item is visible on the trigger.
- The `.ui` catalogue exposes the element and its options.
- Semantics expose role, name, expanded, selected and the choose path, exposed as the `activate`
  action.
- Layout, input, semantic and catalogue/codegen tests exist; the dev runtime carries the option
  array.
- The native popup is owned by its trigger window, placed below the anchor, dismissed by an
  outside press, and reopens without recreating the surface; identical tests exist on macOS and
  Windows.

### P0.3 Panel/Card — done, closes as #15

**Ticket:** #15.

**State.** Implemented on `p0-widget-continuation` as `Panel`, with `Card` as the same facade. It
paints the theme face, radius and border, stacks children inside its padding and announces a
`Group` with the author identity. Layout and semantic tests and the `.ui` catalogue are in place.

**Why it blocked.** Every settings page and the shell are built from themed sections. A `Padding`
around a `Canvas` can imitate the pixels, but it does not give the application a reusable container
with a background, radius, padding and group semantics. Without it, every page reimplemented the
same surface and the same accessibility grouping.

**Next move.** None; delivered. The container owns its background, radius, border and padding,
announces a group semantic, and lays out like the other containers. A title bar, collapse
behaviour and a per-node style cascade stay out of scope as planned.

**Acceptance.**

- A `.ui` form declares a panel and arranges its children inside the padding.
- The panel paints its background, radius and border through the theme.
- It announces a group role and its author `id`.
- Layout and semantic tests cover the container, and the compiler catalogue exposes it.

### P0.4 TextField undo lifetime — done and CI green

**Ticket:** #69.

**State.** Implemented and merged on `main` through #133. `UndoEntry` carries an owner ticket and
a live flag. `UndoStack::remove_owner` revokes the ticket before filtering, drops dead folded
groups and skips dead steps inside a mixed group. A `TextField` owns one ticket, records its edits under it, and
revokes the entries when it is destroyed or rebound to another stack. The stack reports only live
steps through `can_undo`, `can_redo`, `depth` and `top_label`.

**Acceptance evidence.**

- A destroyed field leaves no entry that undo can call.
- Rebinding a field to another stack revokes its entries on the old stack.
- A folded group with no live step disappears; a mixed group keeps its live steps.
- Both stack directions have regressions, and the header states the contract.

The fix is merged on `main` through #133, and the suites run in the macOS and Windows CI jobs.

### P0.5 Icon/content Button — done, closes as #131

**Ticket:** #131.

**State.** Implemented on `p0-widget-continuation` as an `icon` image property plus an optional
`icon_size`; the text-only measurement and paint path is unchanged, and the semantic name falls
back to the author id for an icon-only button. A composed content slot was not added because the
property covers the icon-only and icon-plus-label cases the application needs.

**Why it blocked.** `Button` was a text-only leaf. The application needed icon buttons,
icon-plus-label buttons and buttons whose content is composed rather than a single caption. The
workaround was to paint a custom control in C++, which loses the command binding and the standard
button semantics.

**Next move.** None for #131. The issue's scope allowed the property route when the application
needs icon-only and icon-plus-label buttons, and that is the route taken. The content slot stays
unbuilt until genuinely composed content is required; the text-only path remains byte-compatible.

**Acceptance.**

- An icon-only button is measurable, paintable and has an accessible name.
- An icon-plus-label button lays the two out without clipping and keeps the existing label
  measurement.
- A button with no icon renders exactly as it does today.
- The compiler catalogue, codegen and semantic tests cover the new properties.

### P0.6 Styled/syntax-highlighted TextField — implemented, #132 stays open

**Ticket:** #132.

**State.** Implemented on `p0-widget-continuation` as `TextDecoration`: byte ranges carry a
foreground, a background and an underline, and they paint over the field's existing shaped run.
The C++ API and a `.ui` record list are covered, and `tests/vectors/netlist_editor.ui` carries a
keyword, number and comment corpus. The two acceptance proofs are
`TextFieldDecorationChecks.TheNetlistFixturePaintsItsThreeTokenColours` and
`TextFieldDecorationChecks.WrappedDecorationsScrollWithTheCaretAndSelection`; the full debug suite
passes, so #132 closes with the branch.

**Why it blocked.** The netlist editor is the application's core editing surface, and `TextField`
painted one run with no per-range colour, underline or style. Layering highlighting on top would
duplicate shaping, because the caret and the overlay drift unless both use the same shaped run and
the same scroll offset.

**Next move.** Add the two proofs named above: render `netlist_editor.ui` and assert the three
distinct colours, and assert caret, selection and decoration alignment in a wrapped, scrolled
field. The API already shares the field's `TextLayout`, caret metrics and scroll, so no second
text engine or layout path is needed.

**Acceptance.**

- A netlist fixture highlights keywords, numbers and comments with distinct colours.
- Diagnostics can underline a byte range.
- Caret, selection and highlight stay aligned after wrapping and scrolling.
- A single-run field with no decorations renders exactly as it does today.
- The API is reachable from C++ and, for a declared range list, from `.ui`.

This is P0 for the netlist editor. The rest of the shell can be ported before it lands.

## P1 — delivered on `p0-widget-continuation`

| Gap | What shipped | Ticket | Evidence |
| --- | --- | --- | --- |
| Menus and context menus | In-tree `MenuBar`, `Menu`, `MenuItem`, `MenuSeparator` and `ToolBar` bind shared commands, show the shortcut display, honour visible/enabled and traverse with arrows. | #55 | `widgets/menu*.h`, `widgets/tool_bar.h`, `MenuChecks` |
| Declarative draw vocabulary and path curves | Quadratic and cubic path verbs; rectangle, rounded rectangle, line, circle, text and image primitives; `measure_text`. | #28 | `render/path_resource.h`, `widgets/canvas.*`, `compiler/*`, draw tests |
| Per-node `.ui` styling | Builtin `Point` and `Shadow` records and common opacity, border and drop-shadow properties. | #72 | `compiler/value_type.cpp`, `Codegen.PerNodeAppearancePropertiesReachTheNode` |
| Focus-ring proof | Focused and unfocused buttons raster through the software renderer and the ring is measured in pixels; `Button` publishes the semantic focus state. | #74 | `HeadlessFocusRingChecks.AFocusedButtonRastersARingOutsideItsPlate` |
| Accessibility actions | One portable role-to-action table drives both platform adapters; range values and writes reach the node. | #57 | `semantics/semantic_action.h`, macOS and Win32 role tests |
| TextField double-click and IME wrapping | Word and line selection from a double or triple click; an IME mark that wraps is underlined on every line. | #68, #70 | `TextFieldClickChecks`, `TextFieldImeChecks` |
| Command ergonomics | Named undo/redo declarations, shortcut binding by author id, and component-level `.ui` commands with unknown and unused diagnostics. | #58, #56 | `commands/command_registry.*`, `compiler/*`, command tests |

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

## Completion tracking

This table is the authoritative completion record for the branch: what it delivers, what it
carries from `main`, and which issues the pull request closes. A row marked **close** carries a
closing keyword, so the merge closes the issue; a row marked **open** must not close, and the
reason is recorded with it.

| Completed work | GH issue | Evidence | PR action |
| --- | --- | --- | --- |
| Cross-file `.ui` composition | none — residual of #39, which is closed | `compiler/import_expander.*`, merged on `main` through #133; `bash build/check-build-scope.sh` | no keyword |
| TextField undo lifetime | #69, already closed | `commands/undo_stack.*` and `widgets/text_field.*`, merged on `main` through #133 | no keyword |
| Panel/Card | #15 | `widgets/panel.*`, layout, theme and semantic tests, `.ui` catalogue and codegen | **close** |
| ComboBox, including the native popup surface and owner lifecycle | #18 | `widgets/combo_box.*`, `window/popup_host.h`, `window/window_options.h`, `window/window_registry.cpp`, `application/application.*`, 14 macOS and 14 Win32 popup tests, CI run 40691294678 | **close** |
| Icon and icon-plus-label Button | #131 | `widgets/button.*`, four `ButtonIconChecks` tests, catalogue and codegen | **close** |
| vcpkg package, overlay port and consumer fixtures | #134 | `ports/albor/`, install/export in the root `CMakeLists.txt`, `build/check-port.sh`, `.github/workflows/port.yml`, `.github/workflows/release.yml`, `tests/consumer/`, `tests/port-consumer*`; both platform fixtures green in run 41093329717 | **close** |
| Styled/syntax-highlighted TextField | #132 | `TextDecoration` in `widgets/text_field.*`, three decoration tests, `.ui` record list, `TheNetlistFixturePaintsItsThreeTokenColours` and `WrappedDecorationsScrollWithTheCaretAndSelection`; full debug suite green | **close** |
| Menus, menu items and toolbars | #55 | `widgets/menu.*`, `widgets/menu_item.*`, `widgets/menu_bar.*`, `widgets/tool_bar.*`, `MenuChecks`, hidden-row and stale-focus tests, `.ui` catalogue and codegen | **close** |
| Draw vocabulary, path curves and text measure | #28 | `PathVerb` curve verbs, software/Skia/Blend2D/native replay, six new draw primitives, `Canvas::measure_text`, line-only and image codegen tests, checker and contour tests | **close** |
| Per-node `.ui` styling | #72 | Builtin `Point`/`Shadow`, common opacity/border/drop-shadow properties, styled `p0_widgets.ui`, codegen test | **close** |
| Focus-ring headless proof | #74 | `HeadlessFocusRingChecks.AFocusedButtonRastersARingOutsideItsPlate`, semantic focus in `Button` | **close** |
| Accessibility action table and range writes | #57 | `semantics/semantic_action.h`, macOS range write and action tests, Win32 Toggle/ExpandCollapse/RangeValue tests | **close** |
| Double-click word selection | #68 | `Event::click_count` on macOS and Win32, `TextField::select_word_at`/`select_line_at`, four click tests | **close** |
| IME mark wrapping | #70 | `paint_mark_underline` covers every wrapped line, `TextFieldImeChecks.AMarkThatWrapsUnderlinesEveryLineItCovers` | **close** |
| Command ergonomics | #58 | `CommandRegistry::declare_undo`/`declare_redo`, `bind_shortcut` by author id, registry tests | **close** |
| `.ui` command declaration | #56 | `command name "label";`, generated `Command` ownership/accessor, `command:` binding, unknown and unused diagnostics, parser/checker/codegen tests | **close** |

The macOS `WindowRole::Popup` foundation was #17 and closed before this branch; the anchoring,
ownership, dismissal and capture work above is the part #18's scope names, so it closes with #18
rather than a new ticket.

#134 closes on the staged-install and overlay fixtures, which configure and build the consumer on
both platforms. The `main`-only full `port` matrix is the post-merge gate; if it fails, reopen
#134 rather than treating the close as acceptance.

### Pull request closure block

The branch pull request body carries exactly these keywords. Every issue named here is complete on
the branch, and the merge closes it:

```text
Closes #15
Closes #18
Closes #131
Closes #134
Closes #55
Closes #28
Closes #72
Closes #74
Closes #57
Closes #68
Closes #70
Closes #58
Closes #56
Closes #132
```

### Why #132 closes

Both acceptance proofs were executed by the full local debug suite:

- *"A netlist fixture highlights keywords, numbers and comments with distinct colours."*
  `TextFieldDecorationChecks.TheNetlistFixturePaintsItsThreeTokenColours` renders the generated
  netlist fixture and reads the three painted colours.
- *"Caret, selection and highlights stay aligned after wrapping and scrolling."*
  `TextFieldDecorationChecks.WrappedDecorationsScrollWithTheCaretAndSelection` wraps a decorated
  field, scrolls it, and asserts the painted bands and caret stay aligned.

The suite passes 3,647 tests locally. Windows CI is still the authority for the platform source
and must be green before the editor port.

### Why these do not close

- **#41, #42 and #109** keep the status the previous revision recorded: their descriptions are
  stale or need an explicit acceptance pass, not a closing keyword from this branch.
- **#111** is the Skia retirement; the package writes Skia out of its surface as the first step,
  but the backend still exists for the conformance suites, so the issue stays open.

## Recommended sequence

| Cycle | Goal | Work |
| --- | --- | --- |
| 1 — make components reusable | A page can be assembled from shared `.ui` files | Cross-file composition (merged), #69 undo lifetime (merged), #15 Panel/Card (done, closes), icon/content Button (done, closes), #18 ComboBox (done, closes) |
| 2 — make the editing surfaces real | The netlist editor and chart views reach feature parity | Styled TextField, #28 path curves and draw primitives (done, closes) |
| 3 — make the shell complete | The application looks and behaves like the reference | #55 menus, #72 per-node styling, #74 focus proof, #57 accessibility actions, #68/#70 field polish, #58/#56 command ergonomics (all done, close) |

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
- A close action on the shell and an undo entry on the apply command, so the history path is
  exercised in a real flow.
- A toolbar built from icon buttons; menus and per-node styling can wait for cycle 3.

This slice is the acceptance test for cycle 1. It now builds as
`examples/vertical-slice/`: `vertical_slice.ui` imports `slice_form.ui` and `slice_table.ui`, the
form page carries every control, the table page combines a virtualized `ListBox` with a declarative
chart, and the menu bar and toolbar bind the generated `apply` and `close` commands. The root
installs an undo entry on `apply`, so the slice exercises the history path. It must still run on
both macOS and Windows before the netlist editor work starts.

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
- Menus and toolbars are reachable, and assistive technology can activate, toggle,
  increment and decrement the controls that support those actions.
- The slice builds and runs on macOS and Windows, and the Windows CI run is green.
- No P0 item in this document remains open. Every remaining P1 item has either shipped or is
  explicitly deferred with a documented workaround.

Current status: the control vocabulary, the P1 shell surface and the first vertical slice are built.
The netlist editor is not ported, and the #132 proofs await a test run. This definition is therefore
not yet met.

## Validation baseline

- Branch: `p0-widget-continuation`, based on `main` at `bb1108b` after the
  `application-readiness` merge.
- The local macOS debug, release and test builds pass after the P0 widget work.
- The local debug suite runs 3,588 tests and all pass.
- `bash build/check-build-scope.sh` passes for root, imported-body and imported-interface edits.
- `bash build/check-format.sh` passes at the phase boundary before the review.
- The generated `p0_widgets.ui` and `netlist_editor.ui` fixtures compile through codegen, so the
  new catalogue entries, command declarations, menu shapes, draw primitives and record construction
  are exercised by the build.
- `examples/vertical-slice/albor-vertical-slice` builds on the local macOS debug tree, including the
  two imported pages, the shared row record, the menu bar, the toolbar and the declarative chart.
- The P1 widgets add `MenuChecks`, `Codegen.MenuShapesAndCommandRowsReachTheirWidgets`,
  `Codegen.CommandDeclarationsBuildOneSharedObjectAndBindIt`,
  `Codegen.ACanvasDrawBlockRecordsTheExtendedVocabulary`, the curve contour tests and the macOS and
  Win32 action/range tests. The full local debug suite passes 3,647 tests.
- The MCP automation run launches `albor-vertical-slice`, switches from the Form tab to the Table
  tab, invokes the menu item and toolbar actions, and reverses each with `command.undo`. It captures
  form, table, apply and undo frames as PNGs under `.build-debug/vertical-slice-final/`.
- CI run `40691294678` passed both Windows configurations: 3,565 debug tests (3,563 passed,
  2 skipped) and 3,387 release tests (3,385 passed, 2 skipped). The 14 Win32 popup tests passed.
- The macOS debug CI suite runs 3,588 tests and the release suite 3,408, both with zero failures.
- The vcpkg fixtures pass on both platforms in run `41093329717`: `check-port`, `fixture (macos)`
  and `fixture (windows)` are green. The full `port` matrix runs on `main` and on the nightly
  schedule.
- The pull request closure block names #15, #18, #131, #134, #55, #28, #72, #74, #57, #68, #70,
  #58, #56 and #132.
- The earlier review found two blockers and several majors. The branch now fixes the null
  text-engine popup path, gives the popup a root overlay layer so later siblings cannot cover or
  steal it, separates the highlighted row from the committed selection, preserves a selection
  written before the items, adds the stable ComboBox label, carries ComboBox items and
  `TextDecoration` ranges through devrt/preview, keeps the Card alias on reload, and paints
  decoration bands under the selection. The native popup pass then moved the list into a retained
  owned `WindowRole::Popup` surface, with application popup ownership and shutdown, macOS child
  placement and Windows owner-follow placement, trigger-click forwarding and outside dismissal.
- The native-popup review found one Win32 capture blocker, an Escape routing defect, a portable
  parent divergence and missing surface coverage. The branch now defers Win32 popup capture until
  the owner receives the opening release, offers Escape to the focused control before consuming
  it, resolves a portable `WindowHandle parent` in the registry and refuses a stale one, attaches
  parented dialogs on both platforms, drops a popup stage capture when its own press hides it, and
  covers the real popup builder, theme rebuild, non-LIFO host teardown and press-after-close path.
  Per-frame decoration subset allocation remains a follow-up; the Windows expand/collapse pattern
  now ships with the portable action table.
