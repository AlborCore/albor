# TextField multiline completion plan

**Date:** 2026-11-10
**Status:** implemented on `issue-205-multiline-text-field` (#205). This document is the record of
what was built and why; the code is the source of truth.
**Scope:** the multiline `TextField` as a self-sufficient scrolling editor, so no `ScrollView`
wrapper is ever required. Anchored at `356be5e`.
**Predecessor:** `d9b0899` (#84), "a multi-line TextField scrolls its own viewport instead of
growing". That change settled the model and left the input and presentation halves open; this
plan closes them and does not reopen the model.

## Verdict

The architecture is right and roughly half the feature is shipped. A multiline field already
wraps at the offered width, grows until its parent caps the height, and then scrolls its own
offset with the caret as the authority. Twenty-four cases in `TextFieldScrollChecks` cover that
half.

What is missing is everything that lets a *user* scroll it, plus the rule that stops a
container from scrolling it twice:

- a wheel over a multiline field does nothing, in or out of a `ScrollView`;
- `PageUp` and `PageDown` fall through to an ancestor, which then scrolls without moving the
  caret;
- the field paints no bar and reserves no gutter, so a scrollable editor looks like a clipped
  one;
- an assistive technology that focuses a multiline field has no action that scrolls it;
- **and a multiline field inside a `ScrollView` is silently inert**: the wrapper measures it
  with an unbounded height, its own range collapses to zero, and `scroll_to` becomes a no-op
  while `ensure_caret_visible` and `autoscroll_for_drag` stop working. That is the defect
  behind "wrap it in a `ScrollView` and things break".

The last item is the plan's first principle: **a `ScrollView` around a multiline field must not
be required, and must not be harmful when written anyway.** The fix is to make the wrapper
inert rather than to forbid it, so existing `.ui` files keep working.

No settled decision is reopened. §8.2 already places measurement with the child, and nothing in
§8.3 requires a container to own the wheel.

## What was verified

Each claim was checked against the tree rather than inferred from the documentation.

| claim | evidence | verdict |
| --- | --- | --- |
| A multiline field scrolls itself | `ensure_caret_visible`, `autoscroll_for_drag`, `clamped_scroll_offset`, `maximum_scroll_offset`, `scroll_to` (`widgets/text_field.cpp`) | confirmed |
| The follow half is complete | `ensure_caret_visible` is called from every write, edit, undo, selection and drag path (ten call sites) | confirmed |
| It handles no wheel | no `EventKind::Wheel` in `widgets/text_field.cpp`; the only widget handlers are `scroll_view.cpp` and `numeric_stepper.cpp` | confirmed |
| It answers no paging key | no `Key::PageUp` or `Key::PageDown` anywhere in `widgets/text_field.cpp` | confirmed |
| It draws no bar and reserves no gutter | no `scrollbar` symbol in `widgets/text_field.cpp`; `measure` subtracts `text_field_padding` only | confirmed |
| A `ScrollView` measures content with an infinite height | `SizeConstraints::at_most(... height = infinity)` (`widgets/scroll_view.cpp`, `measure_content`) | confirmed |
| Therefore the field's range is zero inside a `ScrollView` | `maximum_scroll_offset` is `m_layout.size.height - bounds().height`, and the wrapper arranges the field at its full content height | confirmed |
| A `ScrollView` has no reveal contract for its content | no `ensure_visible` or `scroll_into_view` in the tree; `ListBox` maps `index * row_height` onto `scroll_to` itself | confirmed |
| The composition is untested | no `ScrollView` + `TextField` case in `tests/`, and no wheel case in `tests/text_field.test.cpp` | confirmed |
| The field already carries editor-grade surface | decorations, undo stack, clipboard, IME composition, multiline semantics, `scroll_to` | confirmed |
| `TextField` is `final` | `widgets/text_field.h` | confirmed |
| The theme already carries bar geometry and colours | `scrollbar_extent`, `scrollbar_thumb_minimum`, `scroll_line_extent`, `scrollbar_track`, `scrollbar_thumb` (`style/theme.h`) | confirmed |
| `.ui` codegen special-cases `ScrollView.offset` in three places | sink skip, `bind_offset` emission, evaluator no-op (`compiler/codegen/codegen.cpp`) | confirmed |
| The dev runtime has no `offset` support at all | no `offset` symbol anywhere under `devrt/` | confirmed |
| The ComboBox popup does not involve a `TextField` | the popup is a `ListBox` whose `scroll_view()` hosts the row container (`widgets/combo_box.cpp`) | confirmed |
| The dev engine never receives a `scrollbar` policy from source | the compiler accepts `Scrollbar.off` / `on` (`compiler/value_type.cpp`), the dev runtime reads `always_off` / `always_on` (`devrt/element_table.cpp`), and `ir_serializer` drops a value that is not a literal, which a member access is not | confirmed |

The last three facts are recorded because they constrain the optional and out-of-scope items
below; none of them is part of this feature.

## What is already complete, and stays as it is

Recording this so the work does not drift into a rewrite.

- **Wrap width and growth.** A multiline field wraps at the offered width and measures to its
  wrapped run; the parent's ceiling caps the height. This is the documented contract and it is
  what makes a bare field grow in a `Column`.
- **Caret as the scroll authority.** Every path that moves the caret reveals it: writes,
  edits, paste, undo and redo, selection, drag extension, and the arrow keys.
- **Drag autoscroll.** A selection drag past an edge scrolls the content by the overshoot.
- **Visual-only scrolling.** The offset never touches the value, the caret byte, the selection,
  the clipboard or the undo history.
- **Scroll coordinate space.** The value, the placeholder, the composition mark and the
  decorations share one origin through `displayed_top`.
- **Semantics.** The node carries the value, the selection, the focused, read-only, disabled
  and multiline states, and a value write.

## Rules every change inherits

- **No platform source is touched.** The wheel is already normalized by the adapters and the
  bar is portable geometry, so AD-16 does not apply and `macos-windows.md` gains no entry. State
  this in the commit message so the omission is not read as a missed parity pass.
- **Layout invariance (AD-02).** The new content contract is an unconditional base class and the
  new members are unconditional. Runtime class layout stays identical in Release, Dev and Test.
  No `#ifdef`.
- **No platform type in a public header (AD-05, §3.5).** The bar geometry is `Rect` and `Size`
  only.
- **Dual-engine agreement (AD-25).** Any `.ui` property added here lands in the compiler *and*
  the dev runtime, with a corpus test, in the same change.
- **Explicit identity (AD-11).** No new selector is introduced.
- **Text is not row-virtualized (AD-13).** Do not adopt `ScrollableContent`. Its
  `set_row_extent(float)` is a single uniform pitch, and a wrapped run has per-line ascent and
  descent (`ShapedRun::Line`). A variable-height content contract is a separate decision.
- **Comments and format.** One comment line above a statement. `bash build/check-format.sh`
  passes on every changed C++ file before the change is called done.
- **Tests are self-contained.** No helper functions or shared fixtures; each case builds its own
  engine and field. Run them when the owner asks.

## Item 1 — the wheel

The gap is one branch in `dispatch_event` plus a missing mutator.

### Work

- Add `bool scroll_by(float amount)` to `TextField`, mirroring `ScrollView`: clamp through
  `clamped_scroll_offset`, return whether the offset changed, and `invalidate()` when it did.
  `scroll_to` stays as it is.
- Handle `EventKind::Wheel` in `dispatch_event` after the `disabled()` guard and before the
  `KeyDown` branch:
  - a single-line field returns unhandled, because it has no range;
  - the delta is `-event.scroll * wheel_distance(event, line_extent)`;
  - `event.handled = scroll_by(delta)`, so a notch that finds an edge stays unclaimed and an
    enclosing viewport takes it. This is the existing §8.3 rule, not a new one.
- A `read_only` multiline field still scrolls, because reading a document is not editing it.
- Extract the conversion instead of copying it. `ScrollView::wheel_scale` (theme
  `scroll_line_extent`, `ScrollPrecision::Point` = 1.0) becomes a free function shared by both:
  `widgets/scroll_wheel.h`, `float wheel_scale(const Event&, float line_extent)`, with the
  `SCROLL_LINE_EXTENT` fallback beside it. It lives in `widgets/` rather than `core/` because it
  reads an `Event`, and `core/` depends on no other module. Two copies of this will drift.

### Decisions

- **The wheel scrolls the view, not the caret.** The caret stays where it is; the next caret
  move reveals it again. This is what native editors do and it keeps the wheel from silently
  changing the edit position. *Needs owner sign-off* only if a different behaviour is wanted.

### Tests

- A notched notch moves by the themed line extent.
- A trackpad delta moves by its own points.
- An up notch moves toward the leading edge.
- A notch at the leading edge leaves the event unhandled.
- A wheel over a disabled field is unhandled; over a read-only field it moves.
- A wheel over a single-line field is unhandled.

### Acceptance

A multiline field scrolls under the wheel with no wrapper. A notched wheel at the edge chains
to whatever encloses it, and nothing about the caret or the value changes.

## Item 2 — paging keys and document keys

### Work

- Claim `Key::PageUp` and `Key::PageDown` in the unmodified key path. The move is a caret move
  and a scroll together: the caret keeps its on-screen position and the view moves by a page.
- Define a page as the viewport height less one line of overlap, unless the owner prefers a full
  page, so the reader keeps context. Walk the line map accumulating line boxes rather than
  assuming a uniform line height.
- Reuse the column-goal machinery (`caret_goal_x`, `step_up_byte`, `step_down_byte`) so a page
  move behaves like repeated line moves and does not drift horizontally.
- Claim only when the caret or the offset moved, so a page key at the document end chains to an
  enclosing viewport for the same reason the wheel does.
- Add document start and end: `command` or `control` with `Key::Home` and `Key::End` in the
  existing chord branch. Today every accelerator that is not C, X, V, A or Z falls through
  unhandled, so these keys currently do nothing.

### Decisions

- **Page keys are claimed, `Home` and `End` are not repurposed.** A plain `Home` stays the start
  of the caret's line, because that is what the field does today and what the semantics tests
  pin.

### Tests

- A page down moves the caret down and scrolls, keeping the caret at the same screen offset.
- A page up is the reverse.
- A page down at the last line clamps and leaves the event unhandled.
- A page up at the first line clamps and leaves the event unhandled.
- `Ctrl+Home` and `Ctrl+End` reach the document ends, and `Home` alone only reaches the line
  start.

### Acceptance

No paging key dead-ends. A page key over a tall document moves the caret and the viewport
together; at either end the event is left for an ancestor.

## Item 3 — the composition contract

This is the defect the plan exists to remove.

### Problem

`ScrollView::measure_content` gives its content `maximum.height = infinity`, so a multiline
field measures to its full run and the wrapper arranges it at that height. The field's
`maximum_scroll_offset()` is then `run height - bounds height = 0`. With a zero range,
`ensure_caret_visible` computes a target that the clamp pulls back to zero and
`autoscroll_for_drag` finds its write already at the clamp, so both become no-ops; `scroll_to`
clamps everything to zero. The wrapper scrolls, but the caret no longer drives it, selection
dragging no longer reaches past the window, and paging scrolls without the caret.

There is no way for the wrapper to know: `ScrollView` has no reveal contract and the child has
no way to report that it keeps its own viewport.

### Work

- Add a second small content contract beside `ScrollableContent`, in
  `core/self_scrolling_content.h`:

  ```cpp
  // content that keeps its own viewport and offset, which a container must not scroll again (§8.2)
  class SelfScrollingContent
  {
  public:
      virtual ~SelfScrollingContent() = default;

      // report whether this node scrolls its own vertical viewport, which a container must respect
      [[nodiscard]] virtual bool owns_vertical_scroll() const = 0;
  };
  ```

- `TextField` implements it and answers `m_multiline`. The base is unconditional, so AD-02
  holds; the vtable grows identically in every configuration.
- `ScrollView::measure_content` takes the viewport height as well as the width. For a child that
  answers `owns_vertical_scroll()`, the height ceiling is that viewport instead of infinity. A
  `ScrollableContent` child keeps the infinite ceiling, because virtualizing rows is the one
  case that is supposed to grow.
- The result: the wrapper measures the field at the viewport, the field keeps a real range, the
  wrapper's own range is zero, its bar does not draw, and the wheel reaches the field first
  because positioned dispatch is deepest-first. The wrapper is inert rather than harmful.
- Keep the field's own bar decision out of the wrapper: a field inside a `ScrollView` draws its
  own bar if it has one, and the wrapper draws none because it cannot scroll.

### Decisions

- **Make it inert, do not refuse it.** An assert would only fire in debug, which is the
  build-dependent behaviour AD-02 and AD-04 rule out, and refusing the measure would break
  existing `.ui` files. An inert wrapper is source compatible and loses nothing. *Needs owner
  sign-off*, because it adds a public core contract.

### Tests

- A multiline field with a tall value inside a `ScrollView` keeps `maximum_scroll_offset() > 0`
  after measure and arrange.
- The enclosing `ScrollView` reports `maximum_offset() == 0` and draws no bar.
- A wheel event inside the field scrolls the field, not the wrapper.
- The caret follow still scrolls the field, and the caret rect stays inside the clip.
- A `ListBox` inside a `ScrollView` still reports its full content extent, so the
  `ScrollableContent` path is untouched.
- A non self-scrolling child, such as a `Column` taller than the viewport, still grows and
  scrolls, so the wrapper's existing contract is unchanged.

### Acceptance

Wrapping a multiline field in a `ScrollView` changes nothing observable. The composition test
fails on `main` and passes here, which is what pins the regression.

## Item 4 — the bar *(needs owner sign-off)*

Everything else in this plan is behaviour. This is the only item that changes pixels, so it is
separable and the plan is valid without it.

### The four options

| option | gutter | reflow when the bar appears | trailing text | cost |
| --- | --- | --- | --- | --- |
| A. no bar (today) | none | none | clear | a scrollable editor has no affordance |
| B. overlay bar, no gutter | none | none | momentary overlap | needs a fade or a "while scrolling" timer |
| C. gutter reserved whenever multiline | always | none | clear | one `scrollbar_extent` always given up |
| D. as-needed gutter with a two-pass measure | sometimes | **every wrapped line** | clear | worst for text |

Option D is ruled out explicitly. It is `ScrollView`'s model and it is right for a container of
opaque content, but for text it rewraps the whole document at the moment it crosses the box
height, which is exactly while the user is typing the line that crossed it. A text field must
never rewrap because a bar appeared.

### Recommendation

**C in this pass, B as a later refinement.** C reuses the `ScrollbarVisibility` policy, the
theme's `scrollbar_extent`, `scrollbar_thumb_minimum`, `scroll_line_extent` and the track and
thumb colours, and it needs no animation. B is better visually and is where macOS overlay
scrollers land, but it needs a fade, a hit target over live text, and a rule for which of the
two draws above the text. Ship the correct one first.

### Work, if C is chosen

- Add an `m_visibility` member defaulting to `AsNeeded`, and `set_scrollbar_visibility` /
  `scrollbar_visibility` / `scrollbar_visible`, matching `ScrollView`.
- Limit the bar to the multiline mode. A single-line field never scrolls, so it never reserves a
  gutter and never paints a bar, whatever the policy says.
- Reserve the gutter in `measure` before wrapping: the wrap ceiling becomes
  `offered width - text_field_padding * 2 - bar_extent()`. The same subtraction must reach
  `m_measure_width`, because `remeasure` wraps the edit path at that width and the paint and the
  edit path must wrap identically. This is the one implementation hazard in the item.
- Paint the track and the thumb in the reserved strip inside the plate clip, after the text, so
  they are never overdrawn, using the same offset-to-travel mapping as `ScrollView`.
- Claim a pointer press on the thumb before the selection path begins, and steer the offset on
  the move, as `ScrollView` does.
- Extract the thumb geometry as a free function in `widgets/` (`scroll_thumb_rect(offset, maximum,
  track, minimum)`) so the two widgets cannot disagree about where the thumb is.
- Horizontal geometry only: the vertical extent, the caret, the selection, the composition
  anchor and `content_align` are unchanged.

### Tests

- A multiline field that overflows reserves the gutter in its measured width.
- The wrap width excludes the gutter, and an edit after the measure wraps at the same width.
- A multiline field that fits reserves the gutter anyway and paints no thumb, which is what
  keeps the wrap stable when it starts to overflow.
- A single-line field reserves nothing and paints nothing, whatever the policy says.
- `AlwaysOff` reserves nothing and still scrolls; `AlwaysOn` paints a thumb whether or not it
  overflows.
- A press on the thumb claims the event instead of starting a selection, and a drag moves the
  offset.
- The thumb sits at the top at offset zero and at the bottom at the maximum, and never shrinks
  below the theme minimum.

### Acceptance

An overflowing editor shows where it is in the document, and the text does not move when the
bar appears.

## Item 5 — assistive technology can scroll a text area

### Work

- On a multiline field's semantic node, add `scroll_up` and `scroll_down`, each moving one
  themed line, mirroring `ScrollView`.
- Leave the existing `select_all` and `move_caret_*` actions untouched, and do not add an
  absolute jump, because a semantic action carries no target.
- A disabled field exposes no scrolling action; a read-only field does.

### Tests

- A multiline field's node carries both actions and each moves `scroll_offset()` by one line.
- A single-line field carries neither.
- A disabled field carries neither; a read-only multiline field carries both.

### Acceptance

A client that focuses a multiline field can scroll it, using the same action names it already
uses for a `ScrollView`.

## Item 6 — the `.ui` surface

### Work

- **Prerequisite: close the `Scrollbar` divergence for the dev engine.** The compiler accepts
  `Scrollbar.off` and `Scrollbar.on` while the dev runtime reads `always_off` and `always_on`,
  and `ir_serializer` drops the value entirely because a member access is not a literal. Adding
  the property to a second element would inherit that gap, so fix the serialization and the
  spelling in the same change, with a dual-engine corpus case that proves both engines resolve
  the same document to the same policy.
- Add `scrollbar` (type `Scrollbar`, assignable) to the `TextField` element, but only if item 4
  lands. It is the same property name and value set that `ScrollView` and `ListBox` already
  expose, so it needs no new type work:
  - `compiler/value_type.cpp`, the `TextField` property list;
  - `compiler/codegen/codegen.cpp`, the setter table (`TextField.scrollbar` →
    `set_scrollbar_visibility`);
  - `devrt/element_table.cpp`, the `TextField` setter, reusing the existing `read_scrollbar`.

### Deferred, with the reason recorded

- **`offset` and `scrolled`.** A bindable offset would let a console field pin itself to the
  bottom, but the caret already follows edits, which covers the common case, and the cost is not
  local: codegen special-cases `ScrollView.offset` in three places, and the dev runtime has no
  offset support for any element. If this is ever added, generalize those three sites to a small
  predicate over element types rather than adding a second hardcode, and close the dev runtime
  gap in the same change so AD-25 holds.
- **A separate `TextEdit` element.** Not warranted. `TextField` already carries the editor
  surface (decorations, undo, clipboard, IME, multiline semantics) and is `final`; a sibling
  would duplicate most of it. A dedicated editor earns its keep only when line virtualization
  exists, and that needs a variable-height content contract, which is its own decision.

## Item 7 — documentation and the design record

Documentation is part of each item, not a final pass, because the current docs assert the
opposite of the new behaviour.

- `docs/components/text-field.md`
  - Make the height-cap recipe the primary multi-line example: a `SizedBox` with a `height`, or
    any parent that caps the height. Say plainly that a `ScrollView` is neither required nor
    useful.
  - Replace "The field draws no scrollbar — it is an editor, not a ScrollView" with the bar
    policy that item 4 settles, and keep the reason (an editor owns its viewport).
  - Document the wheel, the paging keys, the document keys and the view-only wheel rule.
  - Keep the visual-only invariant and the C++ seam.
- `docs/components/scroll-view.md`
  - Update the cross-link: a multiline field scrolls itself and a `ScrollView` around it is
    inert, rather than merely unnecessary.
- `docs/components/index.md`
  - The one-line purpose of `TextField` may mention the editor mode, if the wording still reads
    as a form control.
- `docs/concepts/layout.md`
  - Note the height cap beside the two phases, since that is the mechanism the recipe uses.
- `albor-design-v4.md`
  - Add a "settled during implementation" paragraph under §8.2 recording the self-scrolling
    content contract, and one under §8.3 recording the rule the code already follows: a nested
    viewport claims the wheel only while it can move, and leaves it unclaimed at the edge. Both
    are additive statements of behaviour that exists; neither reopens a decision.

## Sequencing

| step | items | why here |
| --- | --- | --- |
| 1 | 1, 3 | smallest change that removes the reported defect class: a wheel works, and a wrapper stops being harmful. Land together, because both change what nesting means |
| 2 | 2 | paging is independent and small, but it should follow the wheel so the edge-chaining rule is already in place |
| 3 | 4, 6 | the bar is the only pixel-visible change and the only sign-off, so it lands alone and after the behaviour is settled |
| 4 | 5 | independent of the rest, and the cheapest to review on its own |
| final | 7 | each step carries its own documentation; this row is only the closing read across the three component pages |

The optional `.ui` `scrollbar` property rides with step 3, because that is the only step that
needs it.

## Cross-cutting test matrix

| area | file | cases |
| --- | --- | --- |
| wheel | `tests/text_field.test.cpp` | notch, trackpad, edge fallthrough, disabled, read-only, single-line |
| paging | `tests/text_field.test.cpp` | page down, page up, both clamps, caret screen position, document keys, plain `Home` |
| composition | `tests/scroll_view.test.cpp` | self-scrolling child keeps its range; wrapper reports zero; `ListBox` path unchanged; tall `Column` still scrolls |
| bar | `tests/text_field.test.cpp` | gutter in the measured width, wrap width, thumb geometry, thumb drag, three policies, no reflow |
| semantics | `tests/semantics.test.cpp` | `scroll_up` and `scroll_down` present and effective, absent when single-line or disabled |
| `.ui` | `tests/compiler_codegen.test.cpp`, dev runtime corpus | the `scrollbar` setter is emitted; the two engines agree |

No helpers: each case builds its own `TextEngine`, `BindingEngine` and field, per the repository
rule.

## Out of scope

- **The ComboBox popup defect.** The popup is a `ListBox` over a `ScrollView`, with no
  `TextField` involved, so it is a separate issue. It shares the nested-scroll theme but not this
  code path.
- **Horizontal scrolling for a single-line field.** A real gap: a single-line field measures
  unbounded, the parent clips it, and `ensure_caret_visible` returns early, so a long value can
  clip its own caret. Fixing it contradicts the documented "a single-line field never scrolls"
  rule, so it needs its own decision rather than a rider here.
- **Line virtualization.** The whole value is shaped at once. A large document needs a
  variable-height content contract, which AD-13's uniform `row_extent` cannot express.
- **Tab to indent, bracket matching, syntax-aware editing.** Not this feature.
- **RTL scroll and caret geometry.** Deferred with ICU in §9.2.
- **A separate `TextEdit` element.** See item 6.

## Risks

- **The composition change touches every scrolling container.** `ScrollView` is the base of
  `ListBox` and therefore of the `ComboBox` popup, and it is a public container applications
  place directly, so the new ceiling rule must be proven neutral for every child that does not
  implement the contract. The `ListBox` and tall `Column` cases in the matrix exist for that
  reason.
- **The bar is the only visual regression surface**, and the wrap-width subtraction is the
  easiest thing to get inconsistent between `measure` and `remeasure`. The edit-after-measure
  test pins it.
- **Public API growth.** `SelfScrollingContent`, `scroll_by`, and a possible `scrollbar`
  property are additions to a public surface. Each is small, but they should land with the
  documentation that explains them.
- **The paging semantics are the most judgement-heavy behaviour** and the easiest to argue
  about later. Pinning the caret's screen position in a test is what makes the choice legible.
- **Item 6 inherits an existing AD-25 gap** in the `Scrollbar` value. Fixing it is a
  prerequisite rather than a rider, because a second element must not expose a property the dev
  engine silently ignores.

## Definition of done

- A wheel over a multiline field scrolls it, and a notch at either edge chains outward.
- `PageUp` and `PageDown` move the caret and the viewport; no paging key dead-ends; `Ctrl+Home`
  and `Ctrl+End` reach the document ends.
- Wrapping a multiline field in a `ScrollView` changes nothing observable, and a test fails
  before the change and passes after.
- The bar policy is decided, implemented and painted, and no document rewraps because a bar
  appeared.
- An assistive technology can scroll a multiline field.
- The docs state the height-cap recipe, the wheel, the paging keys and the inert wrapper.
- `bash build/check-format.sh` is clean on every changed file.
- No platform source changed, so `macos-windows.md` is unchanged and the commit message says
  why.

## Decisions taken

1. **Bar policy: C**, the reserved gutter. The owner chose it, and the field implements it. D stays
   ruled out for the reflow reason above; B remains a possible later refinement once a fade
   exists.
2. **The wheel scrolls the view, not the caret.**
3. **A page is the viewport less one line of overlap.**
4. **`SelfScrollingContent` is a base class**, as written, and `TextField` implements it.
5. **`scrollbar` joined the `.ui` surface.** The `Scrollbar` value now crosses the IR in its
   written spelling (`Scrollbar.off` / `on`), which the dev runtime reads. `Align` and `Weight`
   travel with it, because all three are static named values the checker resolved; the runtime
   reads `Align` and `Scrollbar` today and ignores `Weight` until it has a reader.
