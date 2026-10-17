# ISSUE-16 Code Review — ListBox: selection model and keyboard navigation on ScrollView

Branch: `issue-16` (worktree `/Users/rogelio/Projects/albor-issue-16`), base `main@d9b0899`.
The whole change set is the uncommitted working tree: 31 modified files + 11 new files.

Reviewed by: code-reviewer agent, 2026-10-16.

## Scope of review

Every changed and new file was read: the `SelectionModel` (`model/selection_model.{h,cpp}`), the
`ListBox` widget (`widgets/list_box.{h,cpp}`), the semantic additions (`SemanticRole::List`,
`SemanticStates::selected`), the row-enumeration contract (`core/scrollable_content.h`,
`model/scrollable_rows.h`, `widgets/scroll_view.{h,cpp}`), the compiler catalog and codegen
(`compiler/value_type.cpp`, `compiler/codegen/codegen.cpp`), the Dev/Test parity surfaces
(`devrt/element_table.cpp`, `preview/preview_host.cpp`), the macOS accessibility bridge and its
test, all new/changed test files, the example changes and the docs.

## Verification performed

| Check | Result |
| --- | --- |
| `cmake --build --preset debug` (touched TUs rebuilt) | clean, no warnings |
| `./.build-debug/tests/albor-tests` | 2785 tests, all pass |
| `bash build/check-format.sh` | **FAILS** (see Low-1) |
| `bash build/check-build-scope.sh` | ok |
| `bash build/check-link-boundary.sh .build-debug/widgets/libalbor-widgets.a` | ok |
| Generated-probe runs of `albor-codegen` | three confirmed defects (High-3, Medium-1, Low-2) |

## Verdict

Solid unit-level work with good architecture alignment: the selection model sits in `model/`
(§3.2, §18.3), keys are identity values (AD-11), rows reuse the AD-13 repeater, the `ListBox`
composes the `final` `ScrollView`, no `#ifdef` anywhere (AD-02/AD-04 hold), and the generated
reader follows the §13.3 plain-function shape. The generated-fixture compile proof
(`tests/vectors/list_panel.ui`) is a good practice.

However, **the feature does not work end-to-end on the real event path**, **pointer selection
is missing entirely** although issue #16 puts it in scope, and the compiled/Dev dual-engine
conformance (AD-25) is broken for `selected: -1` and for guarded-repeat source updates. The
unit tests pass because they call `ListBox::dispatch_event` directly, bypassing the dispatcher.

---

## High

### High-1 — Keyboard navigation is unreachable on the real event path; the internal ScrollView eats the navigation keys

`widgets/list_box.cpp:76-135` (`ListBox::dispatch_event`) claims `ArrowUp/ArrowDown`,
`Home/End`, `PageUp/PageDown`, `Return/Space` — but that method only runs when the event is
*offered to the ListBox*. The keyboard path offers events to the **focused node first and then
bubbles up through its ancestors** (`input/input_dispatcher.cpp:42-54`, `route_up`; called from
`input/input_controller.cpp:159`). Focus is placed on the **deepest** node under a click
(`input/input_controller.cpp:69-91`, `focusable_under`), and `FocusManager::focusable`
(`focus/focus_manager.cpp:55-67`) accepts **every** visible, enabled node with area — there is
no opt-out, so the internal `ScrollView`, the row container, and every generated `Label` row are
all in the focus ring and can hold the focus.

Consequence in a real (generated) application:

1. The user clicks in the list. Either nothing claims the press (rows/labels do not claim keys
   or presses; `ScrollView::dispatch_event` claims a press only on the scrollbar thumb,
   `widgets/scroll_view.cpp:129-149`) or the thumb was dragged — either way
   `focusable_under` can land the focus on a row `Label` inside the internal `ScrollView`.
2. The user presses ArrowDown. `route_up(row)` walks row → row container → **internal
   ScrollView**, and `ScrollView::dispatch_event` claims unmodified
   `ArrowDown/ArrowUp/PageDown/PageUp/Home/End` at `widgets/scroll_view.cpp:157-179` and scrolls
   one line instead.
3. `ListBox::dispatch_event` is never reached: the bubble stops at the child that claimed the
   event (`input/input_dispatcher.cpp:46-47`). `m_focused` and the selection never move.

The design premise (plan D2: "the ListBox is the single focusable node in the FocusManager
ring; rows are not themselves focus-ring members") is not implementable with the current
`FocusManager` — nothing in this change gives a node a way to declare "not focusable". So rows
*are* ring members in practice, and any descendant focus starves the ListBox.

Every keyboard test passes because it calls `fixture.box.dispatch_event(down)` directly
(`tests/list_box.test.cpp:324` and the rest), which no real dispatcher ever does for this tree
shape. There is no test that routes a key through `InputController`/`InputDispatcher` into a
populated `ListBox`.

Suggested fixes (any one of):
- Have the ListBox claim `PointerDown` inside its bounds and `request_focus` itself, so a click
  always moves the focus to the list, and additionally prevent descendants from holding focus;
- Add the framework mechanism D2 assumed (a `focusable` opt-out on `UiNode`/`FocusTarget`), set
  it on the internal `ScrollView` and the row subtree, and route keys from the ListBox;
- Or intercept the navigation keys *before* the child in the key path (e.g. the ListBox marks
  the key handled when it sees it during `route_down`, which requires the ListBox to sit on the
  focused-node path by construction).

At minimum, add a dispatcher-level test: focus a row, dispatch ArrowDown through
`InputController`, assert the selection moved.

### High-2 — Pointer selection is not implemented, although issue #16 puts it in scope

Issue #16 ("Requirements added from an application POC") states the application "selects from
long lists with **both pointer and keyboard**", and the plan (D3) itself says the widget's job
is to "host, measure, and **hit-test**" the rows. The implementation has no pointer path at all:

- `ListBox::dispatch_event` (`widgets/list_box.cpp:76-135`) only ever inspects
  `EventKind::KeyDown`; `PointerDown/PointerUp` fall through untouched.
- No other code selects a row: the rows do not claim presses, the internal `ScrollView` only
  drags the thumb, and nothing connects a row press to `ListBox::select`.

Clicking a row today does nothing (and, per High-1, does not even move the focus). Nothing in
`ISSUE-16-PLAN.md` §8 (deferred) records this as deferred, and `docs/components/list-box.md`
silently omits any pointer interaction. This is a core issue requirement, not a nice-to-have;
if it is consciously cut, it must be recorded in the plan/docs as deferred with the
`ComboBox` (#18) dependency made explicit.

### High-3 — Guarded virtualized repeat never re-applies `set_item_count`; generated `key_at_N` then reads the vector out of bounds

The unguarded virtualized repeat updates the ListBox's row count on every source change:
`compiler/codegen/codegen.cpp:1742-1748` emits
`self->m_node_<p>->set_item_count(self->m_repeat_items_<n>.size());` inside
`evaluate_repeat<N>`. But a **guarded** repeat (a `Repeat` inside an `if` under the ListBox)
takes the other branch at `compiler/codegen/codegen.cpp:1739-1740`, which calls
`self->reconcile_<n>()` — and the virtualized branch of `reconcile`
(`compiler/codegen/codegen.cpp:2858-2862`) emits only:

```cpp
m_repeat_items_0 = m_repeat_source_0.get();
m_repeat_0->set_items(m_repeat_items_0);
```

— no `set_item_count`. Confirmed with a generated probe (`if (show) { ListBox { Repeat ... } }`,
built with `tests/vectors/task.manifest`): the generated `Impl::reconcile_0()` contains no
`set_item_count` call, while the unguarded materialize path does.

Consequences, since `ListBox::m_item_count` and `m_repeat_items_0` now diverge after any source
mutation while the guard is open:

- **Out-of-bounds read**: the generated reader indexes unchecked —
  `compiler/codegen/codegen.cpp:2744-2748` emits
  `return Impl::key_of_0(raw, self->m_repeat_items_0[index]);`. When the source shrinks,
  `ListBox::select`/`jump_to`/`activate` call `key_at(index)` for `index < m_item_count`
  (the stale, larger count — clamps at `widgets/list_box.cpp:219, 352, 389`, `key_at` calls at
  `widgets/list_box.cpp:225, 358, 391`), which reads
  `std::vector::operator[]` past the end. Undefined behaviour reachable from ordinary data
  updates in a compiled application.
- **Stale navigation bound**: when the source grows, `Home`/`End`/`PageDown` clamp to the old
  count and `select(ItemKey)` stops scanning early.

Fixes:
- Emit the same `set_item_count` line in the virtualized branch of `reconcile`
  (`codegen.cpp:2858-2862`) when `slot.list_host`, or route the guarded case through the same
  list-host update as the unguarded one; and
- Bounds-check (or clamp) inside the generated `key_at<N>` (`codegen.cpp:2744-2748`) so a
  desynchronized count degrades to an empty `ItemKey` instead of UB. Note the same unchecked
  reader is a latent trap for any future imperative `set_item_count` misuse.

Also note: when a guard closes, `emit_repeat_teardown` removes the rows
(`codegen.cpp:2834`) but leaves `m_item_count` non-zero on the hidden list — navigation
then "selects" keys of items that no longer exist. The same `set_item_count(0)` on teardown
would fix that too.

---

## Medium

### Medium-1 — `selected: -1` clears in the Dev IR path and in the docs, but selects the last row in the compiled path (AD-25 violation)

Three surfaces disagree about the same `.ui` input:

- The catalog declares `selected` as `Int` (`compiler/value_type.cpp:321-324`), and the
  type checker accepts negative `Int` literals (unary minus on a numeric operand keeps its type,
  `compiler/type_checker.cpp:1727-1729`).
- The compiled path emits the raw literal into an unsigned setter:
  `compiler/codegen/codegen.cpp:2236-2241` generates `n0->set_selected_index((-1));` (verified
  by running `albor-codegen` on `ListBox { selected: -1; }`). `set_selected_index` takes
  `std::size_t` (`widgets/list_box.h:54`), so `-1` converts to `SIZE_MAX` and
  `ListBox::select` (`widgets/list_box.cpp:214-228`) clamps it with
  `std::min(index, m_item_count - 1)` — **the last row becomes selected**.
- The Dev IR path clears instead: `devrt/element_table.cpp:339-345` reads the value with
  `read_int` and calls `node.clear_selection()` for any negative index.
- The docs promise the clearing behaviour: `docs/components/list-box.md:10` —
  "`selected: -1` clears the selection".

So the compiled build and the Dev build produce different observable behaviour for the same
`.ui`, which is exactly what the dual-engine conformance suite (§16, AD-25) exists to catch,
and the docs match neither path. Fix: give the widget an unsigned-safe "clear" representation
end to end — e.g. make the compiled path treat a negative literal as `clear_selection()`
(a catalog/`setter_of`-level special case or a signed setter `set_selected(long long)`), and
add a codegen test asserting the generated call.

### Medium-2 — No node of a ListBox ever publishes the real keyboard focus

The issue's headline requirement is that focus be "observable outside the widget". The ListBox
reports `focused` on rows from the **internal** index only (`widgets/list_box.cpp:174`,
`node.set_focused(item == m_focused)`), and `ListBox::semantic_node()`
(`widgets/list_box.cpp:137-151`) never calls `node.set_focused(Widget::focused())`. So:

- when the FocusManager holds the focus on the ListBox, no semantic node says so (contrast
  `TextField`, which wires its caret state through `set_focused`);
- when the focus is elsewhere, rows still announce `focused == true` for `m_focused`.

The plan explicitly scoped the *general* "nothing sets `focused`" gap out (plan §2.1), but this
widget is the one the issue pins the requirement to, and its own node could publish the real
state today: `Widget::focused()` is public (`widgets/widget.h:105`). At minimum set
`node.set_focused(focused())` in `ListBox::semantic_node()`, and consider gating the row-level
`focused` flag on the list actually holding the focus.

### Medium-3 — Dev/Test parity gaps for the ListBox in the IR evaluator (AD-25)

`devrt/element_table.cpp` gained only `ListBox.selected/row_height/overscan/scrollbar/disabled`
(lines 339-377). Missing:

- `ListBox.viewport_width` / `ListBox.viewport_height` — the catalog declares them
  (`compiler/value_type.cpp:323`) and both new `.ui` fixtures use them
  (`tests/vectors/list_panel.ui:10-11`, `examples/form/widget_form.ui:100-101`), but the Dev IR
  loader silently drops them (`devrt/reload_adapter.cpp:57` discards the `apply_property`
  result). A Dev-evaluated list has a zero viewport and lays out nothing. (The same gap
  pre-exists for `ScrollView.viewport_*` — `devrt/element_table.cpp:317-331` — but this change
  doubles the affected surface instead of closing it.)
- `set_key_reader` / `set_item_count` are not reachable from Dev at all, so a Dev-mode ListBox
  can never hold rows or a selection — Dev/Test parity of the whole selection feature
  (§16, AD-25) is compile-only today.

Either add the pair-tracked viewport entries mirroring what codegen emits
(`set_viewport_extent(Size{w, h})` with the same track members), or record the Dev-evaluator
limits in the plan's deferred list.

### Medium-4 — `SemanticRole::ListItem` announces as a generic group on macOS

`accessibility/accessibility_bridge.macos.mm:33-36` maps `Canvas`, `Group` **and `ListItem`**
to `NSAccessibilityGroupRole`. Before this change no real tree ever produced a `ListItem`;
now every ListBox row announces as `AXGroup` with a selected/focused state — screen readers
will not present it as a list row (no row semantics, and rows are not exposeable as
`AXRow`-style children of a list). Consider mapping `ListItem` to `NSAccessibilityRowRole`
(with the list's rows as its children), or at least record the gap, since this branch is the
first thing that makes `ListItem` reachable in production trees.

---

## Low

### Low-1 — The repository's own lint fails on the current tree

`bash build/check-format.sh` exits 1 with
`tests/conditional/accessibility_role.macos.mm: No such file or directory`: the rename is
unstaged, so `git ls-files --cached` (build/check-format.sh:59) still lists the deleted path
and clang-format cannot read it. The plan's own checklist (plan §6) requires a passing
check-format; stage/commit the rename (or delete the path from the index) before review hand-off.

### Low-2 — Duplicated `(void)self;` emission block in the generated handler body

`compiler/codegen/codegen.cpp:3017-3022` contains the same guarded statement twice, with
identical comments:

```cpp
// a body that reads no context still carries it, so the generated signature stays uniform (§13.3)
if (handler.body.empty())
    line(2, "(void)self;");
// a body that reads no context still carries it, so the generated signature stays uniform (§13.3)
if (handler.body.empty())
    line(2, "(void)self;");
```

Verified in generated output: every empty handler body (e.g. `on_activated: {}` in
`examples/form/widget_form.ui:110-112` and `tests/vectors/list_panel.ui:20-22`) emits
`(void)self;` twice. Harmless at runtime, but it is clearly an editing accident and every
generated file carries the duplicate.

### Low-3 — `key_at` desync is one `set_item_count` away from UB (hardening note)

Beyond High-3: `widgets/list_box.cpp:410-416` trusts that `m_item_count` never exceeds the
backing collection, and the generated reader (`codegen.cpp:2748`) does not bounds-check. The
invariant is enforced only by two codegen call sites. A `ScrollableContent`-backed list that is
handed an inconsistent count via the public `set_item_count` (`widgets/list_box.h:45`) will
index the reader out of bounds through `select(ItemKey)`'s scan (`widgets/list_box.cpp:247-253`).
Consider documenting the invariant on `set_item_count` and having the widget ask the reader for
a bounded range, or clamp inside the reader (see High-3 fix).

### Low-4 — Unmotivated test-file rename and a spelling the naming convention does not define

`tests/conditional/accessibility_role.macos.mm` was renamed to
`tests/conditional/accessibility_role.test.macos.mm` (new file, `tests/CMakeLists.txt:65-66`).
Nothing in the plan explains it (plan §7 line 441 still lists the old name), and the resulting
`.test.macos.mm` two-infix spelling is not one of the families AGENTS.md defines
(`<name>.macos.mm` etc.); it only works because `build/check-format.sh:59` greps `\.mm$`. If the
intent is "test files carry a `.test.` infix" (matching `semantic_commands.test.cpp`), add the
composition to AGENTS.md/check-format.sh in the same change; otherwise revert the rename.

### Low-5 — Broken documentation link and anchor

`docs/components/list-box.md:4` links
`[Selection-scoped commands](concepts/commands.md#selection-scoped-commands)`. Relative to
`docs/components/` that resolves to `docs/components/concepts/commands.md`, which does not
exist (should be `../concepts/commands.md`), and `docs/concepts/commands.md` has no
`selection-scoped-commands` heading at all (its headings are "The problem it solves", "In .ui",
"In C++", "The rules", "Undo and shortcuts").

### Low-6 — Minor example/build hygiene

- `examples/form/CMakeLists.txt:24` —
  `target_include_directories(albor-form PRIVATE "${CMAKE_BINARY_DIR}/generated/albor-form")`
  is redundant: both `albor_build_native_record` and `albor_build_ui_component` already add
  exactly that directory (`build/AlborComponent.cmake`, `target_include_directories` in both
  functions).
- `examples/form/main.cpp:64` — `if (form)` is dead: `main` already returned when
  `form == nullptr` at `examples/form/main.cpp:32`.
- `examples/form/widget_form.ui:21` — the edit replaced a blank line with one carrying trailing
  whitespace (`    `), and the file lost its final newline; `widgets/CMakeLists.txt` also ends
  without a newline (kept from before, but the file was edited).

### Low-7 — Test-quality gaps against the plan

- `tests/semantics.test.cpp:181-190` (`TheSelectedAndFocusedStatesAreIndependent`) asserts only
  that both flags are true after both are set — it cannot detect one flag aliasing the other.
  Plan §5.2 asked for "a row can be focused but not selected, and vice versa". The ListBox-level
  `FocusAndSelectionAreIndependent` (`tests/list_box.test.cpp:284-296`) covers
  focus-without-selection, but no test produces a **selected-but-not-focused** row at the
  semantic layer (e.g. `clear_selection` after `select`, then assert the row is still not
  focused), nor a focused-but-unselected row through `semantic_child`.
- `tests/compiler_codegen.test.cpp:743` —
  `EXPECT_TRUE(contains(cpp.content, "set_selected_index("));` would also match any longer
  setter name; the sibling assertions pin arguments (`set_disabled(true)`) — pin this one too
  (e.g. `"set_selected_index(3)"`).
- Nothing exercises `move_selection`'s page arithmetic against a viewport whose height is not a
  multiple of `row_height` (truncation direction, `widgets/list_box.cpp:399-408`), nor
  `reveal` for a row taller than the viewport.

### Low-8 — Cosmetic robustness notes in the widget

- `widgets/list_box.cpp:104,110` — `static_cast<int>(page_row_count())`: a viewport of ~2^31
  rows overflows the `int`; the arithmetic that follows is `long`-based already, so widening the
  delta to `long` would be free.
- `widgets/list_box.cpp:336-338` — `long` for the clamp is 32-bit on Windows (LLP64); with
  realistic item counts this is theoretical, but `long long` costs nothing and the file is
  cross-platform.
- `model/selection_model.cpp:7-17` — `select(ItemKey, NO_INDEX)` would store a key while
  `empty()` reports true (the emptiness test is index-only, `selection_model.cpp:31-34`);
  guarding `index == NO_INDEX` in `select` (treating it as `clear()`) would make the model
  total.

---

## What is good

- Layering: `SelectionModel` in `model/` with no `commands` dependency; `ListBox` composes the
  `final` `ScrollView` (plan D1/D2); `commands/` untouched.
- Identity discipline (AD-11): selection stores key + index; keys come from the manifest-declared
  `key` field through a §13.3-shaped plain reader; `CommandArgument`-free design avoids a
  model→commands edge.
- The pending-selection design (`widgets/list_box.cpp:230-240`, applied on
  `set_item_count` at `widgets/list_box.cpp:197-202`) makes `.ui` `selected:` work before the
  `Repeat` materializes — a real ordering trap, handled and tested both at widget level
  (`tests/list_box.test.cpp:741-773`) and by the generated fixture.
- No `#ifdef` anywhere in the change (AD-02/AD-04); Dev/Test-only surface stays in `devrt/` and
  `preview/`; `check-link-boundary` on `albor-widgets` is clean.
- The core contract extension (`core/scrollable_content.h`) stays header-only and
  ui-header-free via a forward declaration, keeping `core/` free of a `ui/` include.
- Test coverage of the widget logic itself is thorough (58 ListBox cases, 12 model cases, plus
  catalog/codegen/dump/bridge coverage), and the generated-fixture compile proof caught the
  index-indirection defect pre-merge per the plan.

## Recommended order of work

1. High-3 (guarded `set_item_count` + bounds-checked `key_at`) — correctness/UB in shipped code.
2. High-1 (key routing) and High-2 (pointer selection) — the feature is not usable end-to-end
   without them; both need a small framework decision (focusability opt-out) first.
3. Medium-1 (`selected: -1`), Medium-3 (Dev IR parity), Medium-4 (`ListItem` role on macOS).
4. Medium-2 and the Low items.
