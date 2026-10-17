# ISSUE-16 Implementation Plan — ListBox: selection model and keyboard navigation on ScrollView

Issue: https://github.com/AlborCore/albor/issues/16
Branch: `issue-16`
Blocks: #18 (ComboBox), #20 (Showcase)
Blocked by: #14 (ScrollView) — **closed and merged**

## 1. Purpose

Deliver a `ListBox` widget built on `ScrollView`, carrying a single-selection model, keyboard
navigation through focus, and keyed/virtualized rows (reusing the repeater infrastructure of
§18.1 / AD-13). The deliverables mandated by the plan rule are:

- semantic tests (role, name, states — including the **selected** and **focused** flags — and the
  activate action, per §10.1 and the issue's "selection must be observable" requirement);
- layout tests (the ListBox hosts and measures rows without owning their content, §8.2);
- a compiler catalog entry (a `.ui` `ListBox` element the type checker and codegen resolve, §11.1 /
  §11.3).

## 2. Current state and dependencies

Read against `main@d9b0899` (the worktree base). Much of what the issue text calls a dependency is
already landed with #14, and must be *used*, not re-implemented:

| Issue requirement | Status | Where |
| --- | --- | --- |
| Home / End / PageUp / PageDown in the key set | **Done** | `Key` enum in `ui/ui_event.h:82-92`; macOS mapping in `platform/macos/platform_window.macos.mm:102-111` |
| Scroll container reports its viewport | **Done** | `ScrollView::viewport_extent()`, `content_extent()`, `maximum_offset()`, `row_height()`, `offset()` in `widgets/scroll_view.h`; actual arranged viewport is `bounds().height` via `UiNode` |
| Viewport extent readable for page up/down | **Done** | `ScrollView::scroll_by(viewport_rect().height)` already implements paging; the ListBox re-derives it from the child's `bounds().height` |
| Keyed rows / repeater | **Done** | `model/keyed_view.h`, `model/scrollable_rows.h` (templated `ScrollableRows<T>` + `ScrollableContent`), `model/item_key.h` |
| Selection span + injection point | **Done** | `commands/selection.h` (`SelectionSpan`, `SelectionSource`); `commands/command_invocation.h` (`CommandArgument` variant carrying `uint64_t` index or `std::string` key) |
| Semantic `focused` flag | **Gap (pre-existing)** | `SemanticNode::set_focused` exists (`semantics/semantic_node.h:148`) but no widget's `semantic_node()` sets it, and `build_semantic_tree` has no focus-manager access (`ui/semantic_tree_builder.cpp`) |

### 2.1 The `focused`-flag dependency

The issue states the semantic layer "carries a focused flag and nothing sets it", and that this is
*not this issue's work* but is a dependency for the semantic tests. Resolution:

- The **ListBox sets `focused` and `selected` itself** on the `ListItem` nodes it exposes, so its own
  semantic tests are fully assertable without fixing the general gap.
- The general gap (Button/TextField/… don't publish their own `focused` state) is **out of scope** and
  is recorded as a follow-up, not fixed here.

## 3. Design decisions

### D1 — Selection model lives in `model/`, single-selection first

§3.2 lists `model/` as "Model interfaces, virtualization, selection models", and §18.3/§18.8 say the
model that fills the span "belongs to the control that owns a selection". We put a **reusable, pure,
single-selection model** in `model/` (so a later multi-select grid can share it) and let the `ListBox`
own one and adapt it to the command injection point. `commands/` stays untouched — it already declares
`SelectionSpan`/`SelectionSource` and implements nothing.

Multi-selection is **deferred**: the issue explicitly leaves "whether multi-selection is in this widget
or in the model module" as the model/-surface decision. The single-selection model is shaped so a
multi-select can replace it without changing the widget's public surface.

### D2 — `ListBox` is composed, not derived

`ScrollView` is `final` (`widgets/scroll_view.h:25`), so `ListBox : Widget` **composes** a `ScrollView`
as its only child and keeps a raw pointer to it (the same pattern `ScrollView` itself uses to drive its
`ScrollableContent`). The ListBox is the single focusable node in the `FocusManager` ring; it owns an
**internal focused index** and a **selection**, and drives the child `ScrollView` to keep the
focused/selected row visible. Rows are not themselves focus-ring members (they are virtualized and
recycled, so a per-row `FocusTarget` would dangle).

### D3 — Rows are application content; the ListBox hosts, measures, hit-tests

Per the issue: "the widget's job is to host, measure, and hit-test it; the job of laying out one row is
the layout module's, and the job of what a row contains is the application's." The ListBox is
**non-templated**. It receives the row count and a type-erased key reader (plain function pointer +
`void*` context, the §13.3 shape) so it can map an index to a `model::ItemKey` for selection and for
`CommandArgument`-keyed activation. The concrete `ScrollableRows<T>` that builds rows is supplied by
the application (or, later, generated by codegen) and adopted into the internal `ScrollView`.

### D4 — selected and focused are two independent states

Per the issue, moving focus through a single-selection list also moves the selection, but the state
set must carry both independently. Concretely the ListBox keeps `m_focused_index` and a separate
`SelectionModel`, and each exposed `ListItem` node reports `selected` (from the model) and `focused`
(from `m_focused_index`) independently. For single selection the two are kept equal on every arrow
move, but they are stored and reported separately, so the semantic tests can assert the distinction.

## 4. File-by-file changes

### 4.1 `model/` — the selection model

**New `model/selection_model.h`** — a pure, single-selection model with no `commands` dependency
(`commands/` sits above `widgets`, which sits above `model`; a model→commands edge would invert the
layering of §18.7):

```cpp
namespace albor::model
{
    // the single selection a list owns, which stores identity and index and reports change (§18.3)
    class SelectionModel
    {
    public:
        SelectionModel() = default;

        // select one item by identity and position; clears any previous selection (§18.1, AD-11)
        void select(ItemKey key, std::size_t index);
        // drop the selection, leaving no item selected (§18.3)
        void clear();
        // report whether nothing is selected (§18.3)
        [[nodiscard]] bool empty() const;
        // hand out the selected identity, which is the empty key while nothing is selected (AD-11)
        [[nodiscard]] const ItemKey& selected_key() const;
        // hand out the selected index, which is SIZE_MAX-equivalent while nothing is selected (§18.3)
        [[nodiscard]] std::size_t selected_index() const;
        // report whether the given identity is the selected one (§18.1, AD-11)
        [[nodiscard]] bool is_selected(const ItemKey& key) const;
        // install the work told about every change of the selection, which is what the signal reports (§18.3)
        void set_on_change(std::function<void()> handler);
    private:
        ItemKey m_key;            // the selected identity; empty while none (AD-11)
        std::size_t m_index = 0;  // the selected index; meaningful only while !empty()
        bool m_empty = true;
        std::function<void()> m_on_change;
    };
}
```

**New `model/selection_model.cpp`** — implement the five mutators/readers above plus the change
notification. Follow the module's existing comment style (one comment line above each statement, §STYLE-GUIDE.md).

**Edit `model/CMakeLists.txt`** — add `"selection_model.cpp"` to the `add_library(albor-model STATIC
...)` source list (currently `"item_key.cpp"` only).

> Decision to confirm at implementation: whether `selected_index()` returns an `std::optional<std::size_t>`
> or a sentinel. Prefer `empty()` + a sentinel of `npos` to avoid dragging `<optional>` into the header;
> the exact spelling is a style choice.

### 4.2 `semantics/` — `List` role and `selected` state

**Edit `semantics/semantic_node.h`**

1. Add a `List` enumerator to `SemanticRole` (after `ListItem`, near `Group`):
   - `List` — "a container of list items that share a selection, which assistive technology groups (§10.1)".
2. Add a `selected` field to `SemanticStates` (after `focused`):
   ```cpp
   // whether the node is the selected row of a list, which is independent of focus (§10.1)
   bool selected = false;
   ```
   The `operator==` on `SemanticStates` is `= default` and picks the field up automatically.
3. Add accessors mirroring the existing `focused` pair:
   - `[[nodiscard]] bool selected() const;`
   - `void set_selected(bool selected);`

**Edit `semantics/semantic_node.cpp`** — implement `selected()` / `set_selected(bool)` exactly like
`focused()` / `set_focused(bool)` (`semantics/semantic_node.cpp:56-64`).

**Edit `tests/semantics.test.cpp`** — the aggregate literal at line 813
(`SemanticStates{.disabled=…, .focused=…, …}`) continues to compile because designated initializers may
omit a member that has a default member initializer; add explicit tests: "ANewNodeIsNotSelected",
"TheSelectedStateCanBeSet", "SelectedAndFocusedAreIndependent", and a `SemanticRole::List` role test.

### 4.3 `core/` — row enumeration on the scrollable content

The ListBox's semantic provider must enumerate the *materialized* rows to overlay `selected`/`focused`
by item index. Today `ScrollableContent` (`core/scrollable_content.h`) exposes only
`content_extent()`, `set_scroll_viewport()`, `set_row_extent()`. Add three read-only accessors that a
plain (non-virtualizing) content reports as "no rows":

```cpp
// how many rows the content has built right now, which is zero for a plain container (§8.2, AD-13)
[[nodiscard]] virtual std::size_t materialized_row_count() const { return 0; }
// the item index the first built row shows, which maps a materialized position to an item (§8.2, AD-13)
[[nodiscard]] virtual std::size_t first_row_index() const { return 0; }
// the built row at a materialized position, or null past the end (§8.2, AD-13)
[[nodiscard]] virtual UiNode* row_node(std::size_t position) { return nullptr; }
```

**Edit `model/scrollable_rows.h`** — override the three above on `ScrollableRows<T>` by forwarding to
`materialized()`/`first_index()`/`child_at(position)` that the class already has (lines 124-130). This
is the only change needed in `core/` + `model/` to let the widget walk rows without the template
parameter.

### 4.4 `widgets/` — the `ListBox`

**New `widgets/list_box.h`** — the widget. Public surface (shape to confirm during implementation):

```cpp
namespace albor
{
    // the list control that owns a selection and navigates it by keyboard over a scroll view (§8, §18.3)
    class ListBox final : public Widget
    {
    public:
        explicit ListBox(BindingEngine* engine, std::string explicit_id = {});

        // measure and arrange delegate to the internal scroll view; the list never grows with its rows (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;
        void arrange(const Rect& bounds) override;

        // override: claim the navigation and activation keys, moving the selection before the scroll view sees them (§8.3)
        void dispatch_event(Event& event) override;

        // override: announce a List role, with ListItem children carrying selected/focused and an activate action (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;
        [[nodiscard]] std::size_t semantic_child_count() const override;
        [[nodiscard]] std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override;

        // hand out the internal scroll view, which the caller adopts the row content into (§8.2)
        [[nodiscard]] ScrollView& scroll_view();

        // install the row count and the plain key reader, which map an index to an identity (§18.1, §13.3)
        void set_item_count(std::size_t count);
        void set_key_reader(model::ItemKey (*reader)(void* context, std::size_t index), void* context);

        // single selection, backed by the model of §4.1
        void select(std::size_t index);
        void select(const model::ItemKey& key);
        void clear_selection();
        [[nodiscard]] bool has_selection() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] const model::ItemKey& selected_key() const;

        // internal keyboard focus, kept separate from selection (§8.3, D4)
        [[nodiscard]] std::size_t focused_index() const;
        void set_focused_index(std::size_t index);

        // viewport/row passthroughs the codegen and the app set on the internal scroll view (§8.2)
        void set_viewport_extent(Size size);
        void set_row_height(float pixels);
        [[nodiscard]] float row_height() const;
        void set_scrollbar_visibility(ScrollbarVisibility visibility);

        // signals
        void set_on_selection_changed(std::function<void()> handler);
        void set_on_activate(std::function<void(const model::ItemKey&)> handler);

        // override: a disabled list refuses input, mirroring the scroll view (§8.3)
        void set_disabled(bool disabled);
        [[nodiscard]] bool disabled() const override;

    private:
        // move the focused/selected index by a signed row delta and scroll it into view (§8.3)
        void move_selection(int delta);
        // move to an absolute index and scroll it into view, which home/end/page keys share (§8.3)
        void jump_to(std::size_t index);
        // scroll the internal view so the given row intersects the viewport (§8.2)
        void reveal(std::size_t index);
        // build the key of the row at an index, which is the empty key past the end (§18.1)
        [[nodiscard]] model::ItemKey key_at(std::size_t index) const;

        ScrollView* m_scroll;                 // owned child; the viewport the list drives (§8.2)
        model::SelectionModel m_selection;    // the single selection this list owns (§18.3)
        std::size_t m_focused = 0;            // the internally focused row, separate from selection (D4)
        std::size_t m_item_count = 0;         // rows the list hosts, which bounds navigation (§8.2)
        model::ItemKey (*m_key_reader)(void*, std::size_t) = nullptr; // §13.3 plain reader
        void* m_key_context = nullptr;
        std::function<void()> m_on_selection_changed;
        std::function<void(const model::ItemKey&)> m_on_activate;
        bool m_disabled = false;
    };
}
```

**New `widgets/list_box.cpp`** — implementation notes:

- **Construction** — construct a `ScrollView` on the same engine, adopt it via `add_child`, keep
  `m_scroll` (the raw pointer) for method calls. Register the selection-change handler to invalidate
  (repaint) and to forward `m_on_selection_changed`.
- **Layout** — `measure`/`arrange` measure/arrange the single `ScrollView` child and copy its size;
  the ListBox is a pass-through container (§8.2). This is what makes the **layout tests** meaningful:
  the list's measured size equals the viewport, never the sum of its rows.
- **Keyboard** — `dispatch_event` claims unmodified `KeyDown` for `ArrowUp`/`ArrowDown`
  (∓1 row), `Home`/`End` (first/last), `PageUp`/`PageDown` (one viewport's worth of rows =
  `floor(m_scroll->bounds().height / row_height())`), `Return`/`Space` (activate the focused row via
  `m_on_activate(key_at(m_focused))`). Modified keys are left to the application (matches
  `ScrollView::dispatch_event`, `scroll_view.cpp:150-152`). Each navigation move updates `m_focused`
  and `m_selection` together (single selection, D4) and calls `reveal` so the row is visible.
  Non-navigation keys are not claimed. Wheel/press/move continue to reach the child `ScrollView`
  through the ordinary pointer path (the ListBox does not intercept those).
- **Semantics** — `semantic_node()` returns role `List`, explicit id, bounds, disabled state, and a
  `selected`-carrying node that is itself not checkable. `semantic_child_count()` returns
  `m_scroll`'s materialized row count (via `ScrollableContent::materialized_row_count()`).
  `semantic_child(index)` builds the row's semantic node by delegating to the row `UiNode` (the same
  `SemanticProviderView` wrapping `Widget` already uses), then sets `selected` when the row's item
  index equals `selected_index()`, and `focused` when it equals `focused_index()`, and registers an
  `activate` action that selects + activates that row. This is what satisfies the issue's
  "selection must be observable while navigating" requirement.

> Implementation nuance: the ListBox's `semantic_child` needs to map a materialized position to an item
> index (`first_row_index() + position`) and to a key, which is exactly what §4.3 exposes. Confirm the
> reuse of `Widget::semantic_child_count`/`semantic_child` (which simply walk the `ScrollView`) is
> *replaced* by the ListBox override, because the plain walk cannot inject `selected`/`focused`.

**Edit `widgets/CMakeLists.txt`** — no source-list change needed (it globs `./*.cpp`); `list_box.cpp`
is picked up automatically. Verify `albor-widgets` already links `albor-model` or add it: the ListBox
uses `model::SelectionModel` and `model::ItemKey`, and the module currently links
`albor-ui albor-semantics albor-text albor-style albor-commands` but **not** `albor-model`. Add
`albor-model` to `target_link_libraries(albor-widgets …)`.

### 4.5 `accessibility/` — macOS bridge

**Edit `accessibility/accessibility_bridge.macos.mm`**

1. Add `case albor::SemanticRole::List: return NSAccessibilityListRole;` to `native_role`
   (line 15-44).
2. Expose the `selected` state on the AppKit element: add an
   `- (BOOL)isAccessibilitySelected` override (mirroring `isAccessibilityFocused`, lines 133-139) that
   returns `published->node.selected()`. This is the concrete answer to the issue's "selected … must
   be observable outside the widget".

No Windows/Linux adapter exists yet (only the macOS bridge is present), so no other adapter change is
required; note this in §8.

### 4.6 `automation/` — role spelling and (optional) selected state

**Edit `automation/semantic_commands.cpp`** — add `case SemanticRole::List: return "list";` to
`role_name` (lines 14-46). This is an exhaustive switch and will fail to compile otherwise once the
enumerator is added. (Optional, only if the tree dump is to carry selection: extend the `tree`/`find`
dump to include a `selected` flag; the current dump emits role/id/name/… — see lines 114+.)

### 4.7 `compiler/` — the catalog entry

**Edit `compiler/value_type.cpp`** — in `ElementCatalog::with_builtins()` add a `ListBox` `ElementType`
(after `ScrollView`, around line 318):

- `.name = "ListBox"`
- `.properties`:
  - `selected` (`Int`, assignable) — the selected row index;
  - `row_height` (`Float`, assignable, scaled_by_density=false);
  - `viewport_width` / `viewport_height` (`Float`, scaled_by_density=true) — delegated to the scroll view;
  - `overscan` (`Float`), `scrollbar` (`Scrollbar`) — delegated;
  - `disabled` (`Bool`).
- `.signals`:
  - `selection_changed` (no parameters);
  - `activated` with `parameter_types = {scalar(TypeKind::String)}`, `parameter_names = {"key"}` —
    the activated row's key (or `Int` for the index; confirm against `CommandArgument`, which carries
    both — prefer the `String` key because identity is the key, AD-11).
- `.accepts_children = true` (the list hosts a repeat of row content, and later a virtualized repeat).

**Edit `compiler/codegen/codegen.cpp`**

1. `widget_info()` (line 292-298): add
   `{"ListBox", {.header = "widgets/list_box.h", .class_name = "ListBox", .needs_engine = true, .needs_text_engine = false}}`.
2. Property map (around line 763): add `ListBox.selected`, `ListBox.row_height`,
   `ListBox.viewport_width`, `ListBox.viewport_height`, `ListBox.scrollbar`, `ListBox.overscan`,
   `ListBox.disabled` → the corresponding setters.
3. Signal map (around line 795): add `ListBox.selection_changed` → `set_on_selection_changed` and
   `ListBox.activated` → `set_on_activate` (with the key passed as the signal parameter).
4. Virtualization: extend the repeat-collection rule at `codegen.cpp:1226` so a `Repeat` whose parent
   element is `"ListBox"` is also `virtualized` (today only `"ScrollView"` is). This reuses the existing
   `ScrollableRows<RecordType>` emission; the generated row content is adopted into
   `list_box.scroll_view()`.

> The full codegen path that emits a `ListBox` *and* its virtualized `Repeat` child is the largest
> piece of the compiler work. If time-boxed, the minimum deliverable is the catalog entry + `widget_info`
> + property/signal maps (so `.ui` can *declare* and *type-check* a ListBox), with the repeat-under-ListBox
> virtualization emission tracked as a follow-up. See §8.

### 4.8 `tests/CMakeLists.txt`

`tests/*.cpp` are globbed automatically, so new suites join the binary without edits. Confirm only that
new `model/selection_model` tests land in the existing `tests/model.test.cpp` (already linked to
`albor-model` at line 109) and the ListBox widget/semantic/layout tests go in a new
`tests/list_box.test.cpp` (linked via `albor-preview` → `albor-widgets`).

## 5. Tests

### 5.1 Model tests — `tests/model.test.cpp`

Extend the existing `ScrollableRowsChecks` / add `SelectionModel` tests:
- selecting stores key and index; `empty()`/`is_selected()` reflect it;
- selecting a second item replaces the first (single selection);
- `clear()` empties and resets the index;
- the change handler fires once per actual change and not on a no-op select.

### 5.2 Semantic tests — `tests/list_box.test.cpp` (and `tests/semantics.test.cpp`)

- `semantic_node()` role is `SemanticRole::List`; explicit id and disabled state travel (AD-11).
- A `ListItem` child for each materialized row, with the selected row reporting `selected() == true`
  and every other row `false`.
- The focused row reports `focused() == true` independently of `selected()` (assert a row can be
  focused but not selected, and vice versa, per the issue's D4 requirement).
- The `activate` action on a row runs the handler with the row's key and selects it.
- These are only assertable once §4.2's `selected` state and §4.3's row enumeration land.

### 5.3 Layout tests — `tests/list_box.test.cpp`

- The list measures to its viewport extent, not to the sum of its rows (a long item list does not grow
  the list's measured height).
- Rows are placed by the scroll view at their item positions, and scrolling the child view moves the
  content (reuse the `RecordingContext`/`ContentNode` doubles from `tests/scroll_view.test.cpp`).

### 5.4 Keyboard tests — `tests/list_box.test.cpp`

- ArrowUp/ArrowDown move focus + selection one row and clamp at the ends.
- Home/End jump to first/last and scroll the view accordingly.
- PageUp/PageDown move by the viewport row count.
- Return/Space activate the focused row (handler receives the row key) and are not passed to the scroll
  view.
- Modified keys are not claimed; a disabled list claims nothing.

### 5.5 Compiler tests — `tests/compiler_value_type.test.cpp`, `tests/compiler_codegen.test.cpp`,
`tests/compiler_type_checker.test.cpp`

- The catalog exposes `ListBox` with the declared properties/signals and `accepts_children`.
- A `.ui` `ListBox` element type-checks and emits a constructor call to `ListBox` with
  `needs_engine`; property and signal assignments map to the documented setters/handlers.

## 6. Build and verification

```
cmake --preset debug
cmake --build --preset debug
bash build/check-format.sh
./.build-debug/tests/albor-tests          # when asked; not run automatically
```

Checklist before marking done: build clean; `check-format.sh` passes on every changed C++ file; the
changed files carry one comment line above each statement, with only `// arrange` / `// act` /
`// assert` markers left on test lines (§STYLE-GUIDE.md, AGENTS.md "Workflow").

## 7. Summary of changed files

New:
- `model/selection_model.h`, `model/selection_model.cpp`
- `widgets/list_box.h`, `widgets/list_box.cpp`
- `tests/selection_model.test.cpp`, `tests/list_box.test.cpp`
- `tests/vectors/list_panel.ui` (the generate-and-compile fixture for the `.ui` ListBox surface)

Edited:
- `model/CMakeLists.txt` (add `selection_model.cpp`)
- `core/scrollable_content.h` (row-enumeration accessors)
- `model/scrollable_rows.h` (override the new accessors)
- `widgets/scroll_view.h` / `scroll_view.cpp` (public row passthroughs over the private content)
- `semantics/semantic_node.h` / `semantic_node.cpp` (`List` role, `selected` state)
- `widgets/CMakeLists.txt` (link `albor-model`)
- `accessibility/accessibility_bridge.macos.mm` (`List` role, `isAccessibilitySelected`)
- `automation/semantic_commands.cpp` (`list` role spelling)
- `compiler/value_type.cpp` (`ListBox` `ElementType`: viewport/overscan/selected/row_height/scrollbar/disabled; `selection_changed` + `activated(key)`)
- `compiler/codegen/codegen.cpp` (`widget_info`, property/signal maps, viewport extents on `ListBox`, virtualized repeat under `ListBox` into `list_box.scroll_view()` with key reader + item count, `key_at<N>` index reader)
- `devrt/element_table.cpp` (`ListBox` factory, setters, element spelling — Dev/Test only)
- `preview/preview_host.cpp` (`ListBox` stub build)
- `tests/vectors/CMakeLists.txt` (generate `list_panel.ui` into the compile fixture)
- `tests/semantics.test.cpp`, `tests/model.test.cpp`, `tests/scroll_view.test.cpp`
- `tests/conditional/accessibility_role.macos.mm`, `tests/conditional/semantic_commands.test.cpp`
- `tests/compiler_value_type.test.cpp`, `tests/compiler_codegen.test.cpp`
- `docs/components/list-box.md`, `docs/components/index.md`, `docs/index.md`, `docs/status.md`, `docs/ui-language/control-flow.md`, `docs/concepts/layout.md`

## 8. Resolved decisions and deferred items

Resolved during implementation:
- **`selected_index()` sentinel** — the model uses a named `SelectionModel::NO_INDEX` sentinel, not
  `std::optional`, so the header stays free of `<optional>`.
- **The `.ui` row content is in scope (AD-01, §11.1)** — an earlier revision deferred the
  repeat-under-`ListBox` emission; that was wrong against the project goal (every widget is usable
  declaratively, no UI work in C++). A `Repeat` whose parent element is `ListBox` now virtualizes like
  one under `ScrollView` (`collect` at `codegen.cpp:1226` region), and `emit_repeat_materialize` adopts
  the `ScrollableRows<T>` into `list_box.scroll_view()` instead of the list node, then wires
  `set_key_reader(&Impl::key_at<N>, this)` and `set_item_count(...)`. `evaluate_repeat<N>` and
  `emit_repeat_teardown` follow the list host. Index→identity generation is a new plain `key_at<N>`
  delegating to the generated `key_of<N>` (§13.3 shape).
- **Full `.ui` parity of the scroll surface** — the `ListBox` catalog carries `viewport_width`,
  `viewport_height` (density-scaled, pair-tracked like `ScrollView`) and `overscan`; `ListBox::set_overscan`
  delegates to the internal view.
- **`activated` signal exposed** — cataloged with a `String` parameter named `key`; `set_on_activate` takes
  `std::function<void(const std::string&)>` (the row identity text), so generated handlers read the
  activated row by its declared `key` field (AD-11).
- **A selection may precede its rows** — `ListBox::set_selected_index` remembers the ask while the list
  holds no rows, and `set_item_count` applies it (statically applied `.ui` `selected:` lands before the
  rows materialize).
- **Signal-less generated handler body** — an empty handler body emits `(void)self;` so the generated unit
  compiles warning-free.
- **Dev/Test parity (§16, AD-25)** — `devrt/element_table.cpp` resolves `ListBox` (factory, setters incl.
  a negative `selected` clearing, element spelling) and `preview/preview_host.cpp` builds the stub.
- **Generate-and-compile fixture** — `tests/vectors/list_panel.ui` builds the generated ListBox + repeat
  unit into `albor-vector-fixture`, which is what caught the index indirection and the empty-handler
  defects before landing.

Deferred:
- **Multi-selection** — deferred; the `SelectionModel` is single-selection. The model/-surface decision
  ("in this widget or in the model module") is recorded but not resolved here.
- **General `focused` flag** — the pre-existing gap that no non-list widget publishes `focused` stays
  open (out of scope; tracked separately).
- **Windows / Linux accessibility** — only the macOS bridge exists; role + selected-state mapping for
  UIA / AT-SPI is future work when those adapters land.
- **`offset` + `scrolled` on `ListBox`** — a scroll position property and its signal over the internal
  view are not exposed; the list scrolls its selection, and a free-scrolling viewport is `ScrollView`.
