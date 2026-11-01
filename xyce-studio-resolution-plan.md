# Xyce Studio issue resolution plan

**Date:** 2026-11-01
**Status:** proposed — decisions marked *needs owner sign-off* are not yet settled
**Scope:** #145, #146, #147, #148, #149 — the five `xyce-studio` issues open on `main` at
`ffefa94` (`v0.2.0`, #144)
**Predecessor:** `gap-analysis-xyce-studio.md` (2026-10-30). That document records the P0/P1
port surface and predates these issues; this plan covers only the delta the first port
uncovered, and links back rather than restating it.

## Verdict

All five issues are real, none is a duplicate, and each is a framework gap rather than an
application mistake. #149 is a small, self-contained fix that unblocks the next Xyce Studio
dialog, and the write half of #147 is nearly as small. #145 and #148 are one coherent piece of
window semantics that should be built together, because they both change `Application`,
`WindowStage` and `InputController` and would otherwise be built twice. #146 is a new platform
integration surface with a shipped application workaround, so it can wait behind the others
without blocking the port.

The work does not reopen a settled decision. Modality, dialog roles and focus restoration are
already specified (§5.1, §8.3, AD-08) and `macos-windows.md` already records modality as the
remaining shared platform gap; `Grid` spans are already specified (§8.2); the automation and
accessibility write path is already specified (§10.1–§10.3). This plan implements specified
behaviour.

## What was verified

Each issue was checked against the tree rather than taken at face value.

| issue | claim | evidence | verdict |
| --- | --- | --- | --- |
| #145 | modality is validated but never read | no `modality` reference in `platform/` or `application/`; `window/window_options.cpp` only refuses Popup/Tool | confirmed |
| #145 | input is routed without a modal check | `Application::bind_window` dispatches to the stage unconditionally (`application/application.cpp`) | confirmed |
| #145 | owned dialogs survive their parent | `m_popup_owners` is filled only by `open_popup`; `destroy_owned_popups` never sees a `WindowRole::Dialog` | confirmed |
| #145 | `WindowModal` needs no live parent | `WindowOptions::validation_error` has no parent rule; `WindowRegistry::create` refuses a stale parent but accepts none | confirmed |
| #146 | there is no file dialog of any kind | no `NSOpenPanel`, `NSSavePanel`, `IFileDialog` or `GetOpenFileName` in any source under the tree; no `nfd` in `vcpkg.json` | confirmed |
| #147 | actions carry no value | `using SemanticAction = std::function<void()>` (`semantics/semantic_node.h`) | confirmed |
| #147 | the field exposes no write action | `TextField::semantic_node` registers `select_all` and four `move_caret_*` only (`widgets/text_field.cpp`) | confirmed |
| #147 | there is no `input.*` command family | `semantic_command_names()` lists `tree`, `find`, `invoke`, `actions`, `set_focused` (`automation/semantic_commands.cpp`) | confirmed |
| #148 | there is no key surface in `.ui` | the language exposes declared signals only (`docs/ui-language/handlers.md`); no element declares a key signal | confirmed |
| #148 | Return does not reach a primary action | `Button` handles Return only when it already holds focus (`widgets/button.cpp`); nothing gives it focus on open | confirmed |
| #148 | no dialog focus policy | `WindowStage` never asks the focus ring to focus anything, and `FocusManager::focusable` needs arranged bounds, so focus cannot be set before the first frame | confirmed |
| #149 | `Grid` exposes only `spacing` | `compiler/value_type.cpp`, element `Grid` | confirmed |
| #149 | codegen never calls `set_columns` | the setter table has `Grid.spacing` only (`compiler/codegen/codegen.cpp`); `GridLayout` defaults to 1 × 1 | confirmed |

Two facts sharpen the plan and are not in the issues:

- `Button` already handles Return and Space when focused, so #148 is about the *window* policy
  that gives the key a target, not about the widget.
- `InputController::deliver_key` force-consumes Escape even when the focused control declined
  it (`input/input_controller.cpp`). That is why Xyce Studio's observer still works, and why
  the framework cannot currently distinguish "a popup consumed Escape" from "nobody did".

## Rules every change inherits

- **Platform parity (AD-16, §5.3).** Any macOS source change carries its Win32 equivalent, its
  test, and a `macos-windows.md` update in the same change. The platform that cannot be built
  on the host is verified in CI before the change is closed.
- **No platform type in a public header (AD-05, §3.5).** The file-dialog API names
  `WindowHandle`, `NativeWindowHandle` and standard types only.
- **Layout invariance (AD-02) and link-time exclusion (AD-04).** Nothing here may sit behind
  `#ifdef` or change class layout per configuration. The automation half of #147 stays in
  `automation/`, which is excluded from Release at link time.
- **Explicit identity (AD-11).** Every new selector — the dialog's remembered control, the
  automation write — uses the author `id`, never an implicit key.
- **Dual-engine agreement (AD-25).** A `.ui` surface change lands in the compiler *and* the dev
  runtime, and a corpus test proves they agree.
- **Generated code already passes the linter.** Codegen changes must produce files that pass
  `bash build/check-format.sh` before the script runs on them.
- **Tests are self-contained** (AGENTS.md). The test double is the only shared object; no helper
  functions, no fixtures.

## #149 — `.ui` Grid always compiles to a 1 × 1 layout

**Severity:** bug, but small and self-contained. **Effort:** S, 1–2 days.
**Unblocks:** the Simulation Parameters and Options dialogs' aligned label/field columns.

### Work

1. **Catalogue (`compiler/value_type.cpp`).** Add `columns` and `rows` to `Grid`, both
   `TypeKind::Int`. Keep `spacing` as it is.
2. **Layout (`layout/grid_layout.h|.cpp`).** Introduce the shape rule in one place, because
   `.ui` children may be dynamic (`Repeat`, `if`) and the count is only known at measure time:
   - add `set_grid(std::size_t columns, std::size_t rows)`, where `0` means *derive*;
   - `columns()` and `rows()` report the effective counts after a measure;
   - derive in this order: both explicit → use them; only `columns` → `rows = ceil(n / columns)`;
     only `rows` → `columns = ceil(n / rows)`; neither → one column of `n` rows (see D1);
   - keep `set_columns(c, r)` as the explicit C++ entry point so `GridSpan` users are unchanged.
3. **Codegen (`compiler/codegen/codegen.cpp`).** Track `Grid.columns` and `Grid.rows` per node
   the way `Slider.min`/`max` are tracked today, then emit one `set_grid(columns, rows)` after
   both are written. Under D1, emit `set_grid(0, 0)` when neither is written, so the compiled
   path and the preview path agree on the derived shape; under the back-compatible alternative,
   emit nothing and leave the C++ default in place.
4. **Dev runtime (`devrt/element_table.cpp`).** Add `Grid.columns` and `Grid.rows` setters that
   write the same node state, and a `Grid` builder that starts from the same shape.
5. **Docs.** `docs/components/grid.md` loses "Cell count is a C++ concern today", gains the two
   properties and the derivation rule; `docs/components/index.md` stays unchanged.

### Decisions

- **D1 — default shape.** Recommend *derive one column of `n` rows* (a `Grid` with two children
  stacks them) rather than keeping 1 × 1. It removes the overlap without choosing an arbitrary
  column count, it is the conventional grid fallback, and the reported reproduction then works
  without extra syntax. A 1 × 1 default is a footgun that nobody chose. This changes
  `GridLayoutChecks.DefaultsToASingleCell` and the documented default, and it may move the three
  children in the preview fixture, whose assertions today cover the gap rather than positions.
  *Needs owner sign-off*; the alternative is to keep 1 × 1 and require `columns` in every
  multi-cell grid.
- **D2 — per-child spans in `.ui`.** Defer. `GridLayout` already supports `GridSpan`, and the
  spec (§8.2) already carries it, but the language has no attached-property form, and adding
  `grid_column`/`grid_row` to every element's common set would expose a Grid concern on every
  widget. Record spans as a C++ concern (as today) and revisit only if a ported dialog needs
  them. Xyce Studio's columns do not.

### Tests

- `tests/layout.test.cpp`: two children in a two-column grid land in adjacent cells; derived
  rows from `columns`; derived columns from `rows`; explicit counts win; a grid whose child count
  changes relayouts; gap arithmetic still holds.
- `tests/compiler_value_type.test.cpp`: `Grid` exposes `columns` and `rows`.
- `tests/compiler_codegen.test.cpp`: the generated body calls `set_grid` with the written values,
  and with `0, 0` when neither is written.
- `tests/conditional/devrt.test.cpp`: the property table and a reload that changes `columns`.
- `tests/preview_host.test.cpp`: the existing rich fixture still reports zero diagnostics.

### Acceptance

- The reproduction from the issue no longer overlaps: under D1 the two labels stack, and with
  `columns: 2` they sit side by side.
- `columns: 2` with four children produces two rows.
- `Grid` with neither property follows D1.
- Compiled and dev-runtime trees have the same shape for the same source (AD-25).

## #145 — enforce window modality and close owned dialogs with their parent

**Severity:** bug plus spec gap; the framework advertises a constraint it does not enforce.
**Effort:** M–L, 4–6 days. **Depends on:** nothing. **Unblocks:** removing Xyce Studio's
hand-written close cascade.

### Work

1. **One ownership map.** Replace `Application::m_popup_owners` with `m_owned_windows`
   (`child id → owner handle`) filled from `WindowOptions::parent` for every created window, not
   only `open_popup`. The entry is erased when the child is destroyed directly, so a stale owner
   never names a dead handle. Keep popup-specific teardown (hide versus destroy) by role.
2. **One blocking predicate.** `Application::input_blocked(WindowHandle)` answers whether the
   target must drop input:
   - an `ApplicationModal` window blocks every other window;
   - a `WindowModal` window blocks its parent, the parent's other children and the parent's
     transients;
   - a modal never blocks itself, nor a window that descends from it, which is what makes nested
     dialogs work;
   - a window with no modality blocks nothing.
3. **Gate at the single choke point.** In `Application::bind_window`, drop a blocked event before
   `WindowStage::deliver` and before `m_input_handler`, so accelerators, commands and the host
   observer all see nothing. This is the spec's "framework-managed behavioural constraint"
   (AD-08) and it is the half that is testable on every platform.
4. **Cascade on close.** `Application::destroy_window(owner)` destroys owned windows first:
   transients (popups) first, then dialogs, deepest first, recursively. The same cascade runs in
   the native `closed` handler. A close that arrives inside a frame pass is deferred as it is
   today; the cascade runs when the deferred teardown is applied, so a child still precedes its
   parent. The owner's own close is still vetoable through `Application::set_close_handler`; a
   granted close cascades, a vetoed close leaves the dialog open. Document that contract (D3).
5. **Validation.** `WindowOptions::validation_error` refuses `WindowModal` without a valid
   `parent`. The registry's existing stale-parent refusal stays; together they make "modal with
   no live owner" impossible at creation.
6. **Activation.** Add `PlatformWindow::activate()` and `Window::activate()`. macOS
   `makeKeyAndOrderFront` exists inside `show()`; Win32 `SetForegroundWindow`/`SetFocus`
   likewise; expose it as a separate call so a window that is already visible can be brought
   back. `Application` activates a new modal dialog after its stage is wired, and re-activates
   the parent when the dialog closes. Every `PlatformWindow` implementation and test double
   implements it, and the headless one counts it.
7. **Remembered invoking control.** At dialog creation, `Application` reads the parent stage's
   focused node and stores its explicit `id` (D6). On close it re-finds that id in the parent tree
   and re-requests focus. An id is used rather than a node pointer because the parent tree may
   have been rebuilt while the dialog was open (AD-11, §18.1).
8. **Native reinforcement.** The portable gate is authoritative. No adapter disables an owner
   window, because the specification makes modality a framework constraint (AD-08); a click can
   still move native key status before the event is dropped, which `macos-windows.md` records (D4).
9. **Document the handler contract.** `Application::create_window` replaces the per-window
   close/closed/resize/frame/input/appearance handlers (`bind_window`). Say so in
   `application/application.h` and in `docs/consuming.md`: a host installs application-level
   handlers, never per-window ones, or it will silently disable routing.

### Decisions

- **D3 — cascade or veto.** Recommend *veto, then cascade*: the parent's close request is
  vetoable once; if granted, every owned window closes with it and no second veto is consulted.
  The alternative (each child can veto the parent) deadlocks a dialog that asks to save before
  closing, so it is not recommended.
- **D4 — how far native enforcement goes.** Decided: the portable gate is the contract. No
  native owner disabling is applied, because AD-08 calls modality framework-enforced; the
  activation caveat is recorded in `macos-windows.md`. A native sheet mode is out of scope.
- **D5 — where dialog conventions live.** Recommend `WindowOptions` (see #148), because role,
  modality and parent are already creation-time there; modality being immutable (AD-08) argues
  against a runtime setter.
- **D6 — remembering the invoking control.** Recommend the explicit-id route. The alternative
  (keep a raw `UiNode*`) dangles as soon as the parent rebuilds a row.

### Tests

- `tests/window.test.cpp`: `WindowModal` without a parent is refused; with a parent it is
  accepted; non-modal windows are unaffected.
- `tests/application.test.cpp` (with the existing `TestPlatformWindow` double):
  - a WindowModal child blocks the parent's events and leaves a sibling window interactive;
  - an ApplicationModal child blocks every other window;
  - a blocked event never reaches `WindowStage::deliver` or `set_input_handler`;
  - closing the parent closes the dialog and the popup, in that order;
  - a vetoed parent close closes nothing;
  - a nested modal child of the dialog still receives input;
  - a destroyed dialog re-activates and refocuses the parent, including after the parent's tree
    was rebuilt.
- `platform/headless`: `activate()` records a count, so the application tests can assert
  activation without a window server.
- macOS and Win32 suites in `tests/conditional/platform_window_popup.test.*`: activation and the
  owned-child close relation, mirroring each other.
- CI run on Windows before the change is done (AD-16).

### Acceptance

- The reproduction in the issue no longer fires the parent's toolbar action.
- Closing the parent closes the dialog; nothing is orphaned.
- `WindowModal` without a live parent is refused.
- Focus and activation return to the parent on close.
- `docs/status.md`, `macos-windows.md` and `gap-analysis-xyce-studio.md` no longer record
  modality as unenforced.

## #148 — dialog keyboard conventions

**Severity:** enhancement that every application otherwise reimplements. **Effort:** M, 3–4 days.
**Depends on:** #145 for activation and the parent/child relation. **Unblocks:** deleting Xyce
Studio's application-level key observer.

### Design

A `DialogConventions` record on `WindowOptions`, honoured only for `WindowRole::Dialog`:

```cpp
struct DialogConventions {
    bool escape_dismisses = true;        // Escape cancels the dialog unless a popup consumed it
    bool return_invokes_default = true;  // Return runs the default control when nothing claimed it
    bool focus_first_control = true;     // the first focusable control takes focus after arrange
};
```

A non-default record on any other role is refused by `validation_error`, which keeps the rule
explicit rather than silently ignored (same shape as the Popup/Tool modality rules). Focus
restoration needs no flag of its own: #145 re-activates the parent and refocuses the remembered
invoking control.

### Work

1. **Escape stops being swallowed, and Return becomes truthful.**
   - `InputController::deliver_key` consumes Escape only when it actually did something (a live
     capture aborted, or the focused control claimed it). When nothing claimed it, the event
     reaches `Application` with `handled == false`.
   - `TextField` stops claiming Return in a single-line field. It still runs `on_commit`, but it
     leaves `handled == false`, because committing is not a text edit; a multiline field still
     inserts the break and claims the key. Today a single-line field claims Return even though all
     it did was commit (`widgets/text_field.cpp`), which is why the default action would never fire
     in the very dialog Xyce Studio is building: the path field holds focus.
   - This is a documented keyboard contract change for `TextField`, not a silent fix.
2. **Default and cancel controls are a portable node interface, not a widget dependency.**
   `albor-application` does not link `albor-widgets` and must not start. Add
   `ui/dialog_control.h` with a small interface implemented by `Button` — the same shape as
   `ui/caret_target.h`:
   ```cpp
   class DialogControl {
   public:
       virtual bool is_default() const = 0;
       virtual bool is_cancel() const = 0;
       virtual void press() = 0;
   };
   ```
   `Button` gains `set_default(bool)` and `set_cancel(bool)`, `.ui` properties `default` and
   `cancel` (both `bool`), and publishes the interface. The stage finds the control by a
   `UiNode::visit` + `dynamic_cast<DialogControl*>` only when a dialog key is unhandled, so the
   cost is one walk per key press, not per frame.
3. **The stage applies the conventions.** In `WindowStage::deliver`, after the tree has routed:
   - unhandled Return and `return_invokes_default` → press the default control if it is visible
     and enabled, otherwise leave the event alone;
   - unhandled Escape and `escape_dismisses` → press the cancel control if there is one,
     otherwise ask `Application` to close the window through the veto path.
   The close itself must go through `Application` (`Window::close` bypasses the close handler and
   detaches the listener), so add `Application::request_close(WindowHandle)` and document it as
   the veto path that a framework-initiated close uses.
4. **Initial focus after arrangement.** `FocusManager::focusable` needs non-empty bounds, so
   focus cannot be requested at build time. Add `WindowStage::focus()` returning the stage's
   `FocusManager&` (today only the private `InputController` owns it), which `Application` and the
   automation host both need. `WindowStage::run_frame` then sets a pending flag after the first
   arranged scene and focuses, in this order: the first focusable control that is neither the
   default nor the cancel control (which is normally the first field), then the default control,
   then the first focusable node. Without that order a dialog whose first focusable node is Cancel
   would focus Cancel.
5. **Opt-out and override.** The application overrides by clearing the record; an application
   that wants more than the conventions uses `Application::set_input_handler` and the `handled`
   flag, which is now truthful.

### Decision

- **D7 — no general `.ui` key surface.** Recommend not adding `on_key` to the language for this
  cycle. The standard keys are framework behaviour, and an application-specific key is a
  shortcut or an input handler. Adding a key signal to every element would widen the language
  for a need the conventions remove.

### Tests

- `tests/input.test.cpp`: Escape with no capture and a declining focus stays unhandled; with a
  capture it still aborts and is handled; a control that claims Escape keeps it.
- `tests/widgets.test.cpp`: `Button` reports default/cancel and presses through the interface;
  a single-line `TextField` commits and leaves Return unhandled, a multiline one claims it.
- `tests/application.test.cpp`:
  - Return commits a focused single-line field and still presses the default control;
  - Return in a focused multiline field inserts a break and does not press it;
  - a focused button or ComboBox that claims Return keeps it, which is the caret-first rule;
  - Escape presses the cancel control, and with none requests the close;
  - a vetoed close leaves the dialog open;
  - the first focusable field is focused on the first frame, not before it;
  - a dialog whose first focusable node is Cancel focuses the default control instead;
  - `focus_first_control = false` leaves focus where it was.
- `tests/compiler_codegen.test.cpp` and `devrt`: `default`/`cancel` reach the button.
- `tests/compiler_*`: `default`/`cancel` are legal `.ui` properties; a non-Dialog role with a
  non-default policy is refused.
- Docs: `docs/components/button.md` gains the two properties, `docs/components/text-field.md`
  records the Return contract, and `docs/consuming.md` describes `DialogConventions`.

### Acceptance

- Escape dismisses a dialog unless a child popup consumed it.
- Return invokes the primary action, including while a single-line field holds focus.
- Focus enters the dialog at the first focusable control and returns to the invoking control.
- An application can opt out per flag and override with an input handler.

### Risk

The Return contract change is the one behaviour change an existing application can notice: a
single-line field no longer reports the key as handled. An application that relied on that to
block a global Return shortcut would now see the shortcut fire. The change is recorded in the
field's documentation, and the shortcut table is consulted only after the field declines, so the
field still wins for every edit it performs.

## #147 — automation and assistive technology can write a TextField

**Severity:** enhancement plus accessibility gap (`IValueProvider::SetValue` is read-only on
Windows, `NSAccessibilityValueAttribute` is not settable on macOS). **Effort:** S–M, 2–3 days
for the write path, +1–2 days for `input.type`. **Depends on:** `WindowStage::focus()` for
`input.type` (added in #145/#148). **Unblocks:** Xyce Studio's `tests_ui` fill scenarios.

### Work

1. **One value action on the semantic node (`semantics/semantic_node.h|.cpp`).**
   ```cpp
   using SemanticValueAction = std::function<bool(std::string_view)>;
   void set_value_action(SemanticValueAction on_write);
   [[nodiscard]] bool has_value_action() const;
   [[nodiscard]] bool write_value(std::string_view value) const;   // refuses while disabled or read-only
   ```
   Mirrors `SemanticRangeAction`/`set_range_value` exactly, including the disabled guard and the
   same refusal contract.
2. **`TextField` wires it (`widgets/text_field.cpp`).** The action replaces the selection through
   the existing `replace_range(selection_start(), selection_end(), text)` path, so undo records
   one step, `on_change` fires, the caret follows, and a multiline field keeps its breaks. It
   refuses when read-only or disabled. This is the real editing path, not a back-door write.
3. **Automation (`automation/semantic_commands.cpp`).** Register `semantic.set_value { id, value }`
   and add it to `semantic_command_names()` (the allow-list is by name, AD-10). Refusals carry
   the same messages as `invoke`: no snapshot, missing id, unknown id, no writable value, or the
   node refused the write.
4. **Accessibility parity (`accessibility/accessibility_bridge.macos.mm`,
   `accessibility/accessibility_platform.win32.c++`).** macOS reports
   `accessibilityIsValueAttributeSettable` for a node with a value action and writes it in
   `setAccessibilityValue:`; Windows implements `IValueProvider::SetValue(LPCWSTR)` by converting
   UTF-16 to UTF-8 and calling `write_value`. Both are the same rule, so they land in the same
   change (AD-16).
5. **`input.type { id, text }` (stretch, same cycle if it stays small).** A host-side command that
   resolves the node by `id` in the active tree, focuses it through `WindowStage::focus()`
   (exposed in #145/#148), and delivers one `EventKind::TextInput` event per code point through
   `WindowStage::deliver`. This exercises the real focus and editing path and gives the acceptance
   test "typing into a focused multiline field" something to drive.
   It needs a `WindowStage*` reachable from the host binding: `Application::window_for_node(*root)`
   already resolves the handle from the root, and `Application::stage(handle)` is public, so no
   new seam is required beyond the `focus()` accessor.
6. **Docs.** `automation.md` gains the two commands; `macos-windows.md` loses the "text value
   writing remains read-only on both" note; `docs/status.md`'s accessibility row follows.

### Decision

- **D8 — one action type, not a per-widget RPC.** Recommend the `SemanticValueAction` above rather
  than a writable `SemanticNode` field, because `SemanticNode` is a value copy rebuilt from the
  widget on every read (`AutomationHost::semantic_tree`). A stored string would be written into a
  snapshot that is thrown away; an action reaches the widget that owns the state. This is the same
  reason `set_range_value` exists.

### Tests

- `tests/conditional/semantic_commands.test.cpp`: set a value; replace a selection; refuse an
  unknown id; refuse a read-only field; refuse a disabled field; the change is visible in the next
  `semantic.tree` dump; the action is listed by `semantic.actions`.
- `tests/widgets.test.cpp`: the value action records one undo step and fires `on_change`; a
  multiline value keeps its line breaks.
- macOS role suite: the value attribute is settable for a field with a write action and the write
  lands. Win32 role suite: `IValueProvider::SetValue` returns `S_OK` and the text changes;
  read-only returns `UIA_E_ELEMENTNOTAVAILABLE`. Both mirror each other.
- If `input.type` lands: typing into a focused multiline field inserts at the caret and wraps.

### Acceptance

- A client can set a TextField value by explicit `id` without going through a fake pointer click.
- Assistive technology can write a text value on both platforms.
- The write goes through undo and `on_change`, so the application observes one edit, not a magic
  state assignment.

## #146 — a native file dialog API

**Severity:** enhancement with a shipped application workaround (`nativefiledialog-extended`).
**Effort:** L, 6–9 days. **Depends on:** nothing; **risk:** the macOS event loop, see below.
**Unblocks:** deleting Xyce Studio's `studio/src/platform/file_dialog.cpp` and the `nfd` vcpkg
dependency.

### API (`application/file_dialog.h`, methods on `Application`)

The option types and the backend seam live in `application/file_dialog.h`; the two entry points
are declared on `Application` (`application/application.h`) and defined in
`application/file_dialog.cpp`. They are methods rather than free functions because `window/` sits
below `application/` and cannot reach the application object to resolve a `WindowHandle`, exactly
as `PlatformEventLoop` already lives in `application/` while its implementation lives in
`platform/`.

```cpp
namespace albor {

struct FileFilter {
    std::string name;                       // "Xyce executables"
    std::vector<std::string> extensions;    // {"exe"} — bare, no dots; empty means any file
};

struct OpenFileOptions {
    WindowHandle parent{};                  // the panel is owned by this window
    std::string title;
    std::vector<FileFilter> filters;
    std::string initial_directory;          // utf-8, empty means the system default
};

struct SaveFileOptions {
    WindowHandle parent{};
    std::string title;
    std::vector<FileFilter> filters;
    std::string initial_directory;
    std::string default_name;
    bool confirm_overwrite = true;
};

// declared on Application:
class Application {
public:
    [[nodiscard]] std::optional<std::filesystem::path> open_file(const OpenFileOptions& options);
    [[nodiscard]] std::optional<std::filesystem::path> save_file(const SaveFileOptions& options);
};

} // namespace albor
```

Both calls are UI-thread only and synchronous; `nullopt` means cancelled, and a platform failure
is reported through the same channel plus a diagnostic on stderr (D9). Neither header carries a
platform type.

### Backend seam

Mirror `PlatformWindowFactory`, which is the pattern the platform adapters already install at
load (§5.3, AD-16):

```cpp
class PlatformFileDialog {
public:
    virtual ~PlatformFileDialog() = default;
    virtual std::optional<std::filesystem::path> open(const OpenFileOptions&, NativeWindowHandle parent) = 0;
    virtual std::optional<std::filesystem::path> save(const SaveFileOptions&, NativeWindowHandle parent) = 0;
};
void set_platform_file_dialog_factory(std::function<std::unique_ptr<PlatformFileDialog>()>);
```

`Application::open_file` resolves `options.parent` through its own registry to
`Window::native_window_handle()`, refuses a stale handle, and hands the opaque handle to the
backend. The headless adapter installs a scripted backend (see tests). Each `platform_install.cpp`
installs its own.

### Platform work

- **macOS (`platform/macos/file_dialog.macos.mm`).** `NSOpenPanel`/`NSSavePanel`, owned by the
  parent (`beginSheetModalForWindow:completionHandler:` when a parent exists, plain modal
  otherwise), `allowedContentTypes` from the filter extensions, `directoryURL`,
  `nameFieldStringValue`, `canCreateDirectories`, `isExtensionHidden`. Paths come back as
  `NSURL.fileSystemRepresentation` → UTF-8 → `std::filesystem::path`.
- **The macOS event-loop risk.** `MacosEventLoop::run` is a hand-written loop in
  `NSDefaultRunLoopMode`, not `-[NSApplication run]`. A sheet therefore needs either a nested
  loop in `NSModalPanelRunLoopMode` that also drains the framework pump, or a change to the main
  loop's mode handling. This is the one genuinely uncertain part of #146 and should be prototyped
  first, before the API hardens. Budget two of the estimated days for it.
- **Windows (`platform/windows/file_dialog.win32.c++`).** `IFileOpenDialog`/`IFileSaveDialog`
  with `SetTitle`, `SetFileTypes`, `SetFolder`, `SetFileName`, `SetOptions(FOS_FORCEFILESYSTEM)`,
  `Show(owner)`, `GetResult` → `SIGDN_FILESYSPATH` → UTF-8. The owner comes from the parent's
  `HWND`. COM needs an apartment on the UI thread that nothing else in the framework has
  initialized: add a one-time `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` guard owned by
  the backend. A thread already in another apartment gets a refused dialog rather than a second
  initialization. Link `ole32`/`uuid` (and `comdlg32` only if the legacy fallback is taken).
- **Headless (`platform/headless/file_dialog.cpp`).** Answers from a queue supplied by the test,
  records the options so a test can assert parent, title, filters and initial directory, and
  returns `nullopt` when the queue is empty (a cancel). This is what lets automation exercise the
  calling flow without a window server.

### Decisions

- **D9 — synchronous, not asynchronous.** Recommend synchronous. `nfd`, which Xyce Studio already
  uses, is synchronous, so a synchronous API replaces the workaround without restructuring their
  `browse()` handler; Windows has no window-owned asynchronous dialog at all, so an async API
  would be a lie on one platform; and §4.3 already covers native modal loops. Document that the
  framework pump is suspended while a native panel is up, so animation pauses.
  *Needs owner sign-off* if a future asynchronous surface is wanted; the backend seam above can
  grow one without breaking this entry point.
- **D10 — `std::filesystem::path`, not UTF-8 `std::string`.** Recommend the path type. It is a
  standard library type, not a platform type, so AD-05 is satisfied; it survives Windows
  round-tripping without a second conversion; and the application already needs a path to open
  the chosen file. Input strings (`initial_directory`, `default_name`) stay UTF-8.
- **D11 — single selection only in v1.** No `multiple` flag and no vector result. Xyce Studio opens
  one Xyce executable and one netlist at a time; multi-select can be added later without changing
  the shape of `open_file` if it is introduced as a separate `open_files`.

### Tests

- `tests/file_dialog.test.cpp` with the headless backend: options reach the backend intact, a
  scripted answer comes back, an empty queue cancels, a valid parent resolves to the live native
  handle, a stale parent is refused, an absent parent opens an unowned panel, and a UTF-8 path
  round-trips.
- `tests/conditional/file_dialog.test.macos.mm`: the panel configuration is asserted without
  showing it (`title`, `allowedContentTypes`, `directoryURL`, `nameFieldStringValue`), plus one
  test that a parented panel is created as a sheet and a completion reverses the block.
- `tests/conditional/file_dialog.test.win32.c++`: the same assertions on the `IFileDialog` object
  before `Show` (`GetTitle`, `GetFileTypeIndex`, `GetFolder`, `GetFileName`), plus the COM
  apartment guard.
- `tests/port-consumer` builds against the installed header, so the package surface is proven
  (AD-19).

### Acceptance

- Open and Save return a chosen path or a cancel, with filters, title, initial directory and
  default name honoured.
- The panel is owned by its parent on both platforms.
- A path with non-ASCII characters survives on every platform.
- A headless test can drive the calling flow.
- `docs/consuming.md`, `docs/status.md` and `macos-windows.md` describe the new surface.

## Sequencing

Each row is one cycle of one or two pull requests, one snapshot
(`bash build/snapshot.sh <label>`), and a green macOS and Windows CI run. #145 and #148 are
deliberately adjacent: they touch the same routing, focus and window files, and building them
back to back avoids a second pass over the same code.

| cycle | issues | effort | exit criterion |
| --- | --- | --- | --- |
| 1 — unblock the next dialogs | #149, #147 (set_value only) | 3–5 d | a `.ui` form can place label/field columns, and a test client can fill a TextField by id |
| 2 — window semantics | #145 → #148, then #147 `input.type` | 8–12 d | modality is enforced and cascades; dialog keys and focus are framework behaviour; typing is drivable |
| 3 — native integration | #146 | 6–9 d | open/save are native, owned and portable; `nfd` can be removed |

Total: 17–26 engineer-days, or roughly four to six weeks for one engineer including review and
both CI platforms. The `set_value` half of #147 can move earlier if Xyce Studio's validation
tests start blocking before #149 lands.

### Why not #145 first

It is the highest-severity item, and it is second only because #149 is a one-to-two-day,
self-contained fix that unblocks a dialog Xyce Studio is already writing. If the maintainer
prefers severity order, swap cycles 1 and 2; neither depends on the other.

## Cross-cutting test matrix

| area | portable suite | macOS | Windows | headless |
| --- | --- | --- | --- | --- |
| modality and cascade | `application.test.cpp` | owned child + activation | `EnableWindow` + activation | activation count |
| dialog keys and focus | `application.test.cpp`, `input.test.cpp` | — | — | first-frame focus |
| Grid shape | `layout.test.cpp`, codegen, devrt | — | — | — |
| semantic write | `semantic_commands.test.cpp`, widgets | settable + write | `IValueProvider::SetValue` | — |
| file dialog | `file_dialog.test.cpp` | panel configuration | `IFileDialog` configuration | scripted queue |

The platform cells are required by AD-16; a platform-only test gap is a defect, not a follow-up.

## Out of scope

- **A native sheet mode for `WindowRole::Dialog` on macOS.** Modality stays a framework
  constraint with the native hints above.
- **Per-child Grid placement and spans from `.ui`** (D2).
- **A general `.ui` key handler** (D7).
- **Asynchronous or multi-select file dialogs** (D9, D11).
- **Any renderer, text-shaping or performance work.** `gap-analysis-xyce-studio.md` already
  ranks that backlog and none of it blocks these five issues.
- **Linux/Wayland parity.** The HAL interface is fixed; the file-dialog backend joins that work
  when the platform lands (§5.3).

## Definition of done

- All five issues are closed by a pull request that carries `Closes #N`.
- macOS and Windows CI are green on the merge commit, including the new platform suites.
- `bash build/check-format.sh` passes, and every changed file obeys the one-comment-line rule.
- `macos-windows.md`, `docs/status.md`, `automation.md`, `docs/consuming.md` and
  `docs/components/grid.md` describe what shipped.
- `gap-analysis-xyce-studio.md` records the closure and the workarounds it removes.
- No `#ifdef` was introduced for a Dev/Test feature (AD-04), and `bash build/check-link-boundary.sh`
  passes.

## What Xyce Studio deletes when this lands

| workaround in `xyce-studio` | replaced by |
| --- | --- |
| `studio/src/platform/file_dialog.{h,cpp}` and the `nfd` dependency | #146 `Application::open_file`/`save_file` |
| the close cascade in `studio_app.cpp`'s `set_close_handler` | #145 owned-window cascade |
| the Escape/Return observer in `set_input_handler` | #148 `DialogConventions` + the default/cancel button roles |
| the explicit `window->show()` after seeding | unchanged — visible `false` at creation is still the way to avoid a blank first frame |
| the `to_utf8` helper | not removed: `Application::open_file` returns a path and the field still stores UTF-8, so a public converter stays a small follow-up |

## Open decisions

| id | question | recommendation | owner |
| --- | --- | --- | --- |
| D1 | does a `Grid` with no `columns` derive one column, or stay 1 × 1? | derive one column | maintainer |
| D4 | how far native modality enforcement goes on macOS | decided: portable gate, recorded in the ledger | — |
| D9 | is the file dialog synchronous? | yes, and documented as suspending the pump | maintainer |
| D10 | path or UTF-8 string result | `std::filesystem::path` | maintainer |

Everything else in this plan is implementation detail that follows from the specification and the
surrounding code, and does not need a decision before work starts.
