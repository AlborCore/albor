# Defect D6: Layout dirty flags have no consumers, and every frame re-shapes every run

Context and the sibling defects live in `RENDERING-DEFECTS.md`. **Partially fixed**: steps 1, 2
and 3 of the plan are done, and the part that needs a proven shape is left open.

## Summary

The ui tree carries `m_render_dirty` and `m_layout_dirty` (`ui/ui_node.h:133,136`),
both set and bubbled by the invalidation paths (`ui/ui_node.cpp:151-173`), but no
production code reads the layout flag. `SceneBuilder::build` re-measures and
re-arranges the whole tree on every build, and every measure constructs a fresh
shaping paragraph with no cache. A purely visual change, such as a colour or a
disabled state, pays the full layout and text shaping cost of the entire tree. The
frame gate that exists (`FramePipeline::build_if_dirty`) is bypassed by the one
example that drives a real window tree.

## Defect

1. **The layout dirty flag is unread.** `m_layout_dirty` defaults true
   (`ui/ui_node.h:136`), is set by `invalidate_layout` and cleared by `clear_dirty`
   (`ui/ui_node.cpp:162-186`), but the only readers outside the definition are tests.
   The record pass consumes nothing from it.

2. **Every build re-runs the full layout.** `SceneBuilder::build` unconditionally calls
   `root.measure(SizeConstraints::tight(metrics.logical_size))` and
   `root.arrange(full_rect)` before recording (`ui/scene_builder.cpp:177-180`), whether
   or not anything asked for a layout.

3. **Measure is the expensive pass.** Every `TextEngine::measure` builds a fresh
   shaping paragraph (`text/text-skia/skia_text_engine.cpp:55-97`) with no cache, so a
   visual-only invalidation re-shapes every text run in the tree at every frame it
   triggers.

4. **The existing gate is bypassed.** `FramePipeline::build_if_dirty`
   (`ui/frame_pipeline.cpp:35-43`) settles the graph and returns no scene when the
   tree is clean, which is the correct whole-frame gate. `examples/chart/main.cpp:75`
   calls `pipeline.build(metrics)` unconditionally on every pacing tick instead.

## Cause

The dirty flags were built for the scheduling side (they drive frame requests through
the root's invalidation handler) and the build side never adopted them. The build is
therefore always a full rebuild, and the only defence against redundant work, the
dirty query, is wired to nothing.

## Plan

### Done

1. **Consume the flags in the build.** `SceneBuilder::build` reads the tree state
   before the passes:
   - when the tree is layout dirty, run measure and arrange as today, then clear the
     layout flags after the passes succeed;
   - when the tree is layout clean, skip measure and arrange and reuse the stored
     bounds, and run only the record pass;
   - when nothing is dirty, produce no scene.
   Constraints travel top down, so a dirty subtree is re-measured from the root; the
   skip path is what saves the visual-only changes.

2. **Route the window tree through the gate.** The chart example builds through
   `build_if_dirty`, so a clean tree costs one settle call and no recording. This
   keeps `frame_count` aligned with what the tree actually asked for (§6.1).

3. **Make the measure that remains cheap.** The shaped-run cache from
   `RENDERING-DEFECTS.md` (Phase B) turns a repeat measure of unchanged text into a
   lookup, which bounds the cost of the full passes that a layout change still pays.

### Still open

4. **Follow-up, later optimization.** Subtree-level relayout and partial repaint, on
   the proven shape for this kind of cache:
   - a side table keyed by node identity, holding one cached entry per item: the
     item's screen bounds and transform, plus a per item dirty tracker; the entry
     slot is a slab index plus a generation, so a re-created node never inherits a
     stale entry;
   - dirty regions are computed per frame and capped at three rectangles, merging
     oversized unions instead of accumulating unbounded region lists;
   - an item that is not dirty registers as a dependency on its own tracker, so any
     change that should affect it re-dirties exactly that item, and the build skips
     its content work entirely — text shaping included — when the item is undrawn;
   - where the surface reports back-buffer age, a small history of the most recent
     dirty regions lets a multi-buffered swapchain repaint only what changed across
     swaps, applied as a clip on stage two.
   This needs per-node measured size retention, which `m_measured_size` already
   stores, and a command list that can be re-recorded in ranges, which the scene plan
   produces.

### The invariant this now rests on

Skipping measure is only safe while every path that can change a measured size asks for
a layout. Widgets that own text do it correctly, because `set_text` calls
`invalidate_layout` (`widgets/label.cpp:77,110`, `widgets/button.cpp:151`), and the tree
mutations do too (`ui/ui_node.cpp:56,71,86,202`). The examples read their properties in
`render` and draw absolutely positioned runs, so their measure cannot change.

This is worth stating because the compiler work in Phase 4 will generate property
observers, and a generated observer that repaints a node whose measured size depends on
that property would silently keep a stale bound. **A property write that can change a
node's measured size must invalidate layout, not only repaint** (§8.2).

A second guard was added, because the flag alone was not enough: the builder remembers
the metrics of the last build and lays out again when they differ. Without it a resize
that no handler invalidated would leave the tree holding the bounds of the old surface,
where before this change the unconditional passes hid the mistake. The cost is that a
window resize always pays a full layout, which is what it has to (§8.2).

## Verification

- A first build measures and arranges, a second build of an unchanged tree does not, a
  visual change does not, and a layout change does again
  (`SceneBuilderLayoutDirtyChecks` in `tests/ui.test.cpp`).
- A skipped layout keeps the bounds the last build placed and still records the frame,
  so a repaint is not lost (`SceneBuilderLayoutDirtyChecks`).
- A resize lays the tree out again with no invalidation, which is the guard against an app
  forgetting one.
- A clean tree produces no scene and a visual change does (`FramePipelineDirtyChecks`).
- The chart example presents only on invalidation: its frame count stays flat while
  the tree is clean.
- The existing layout suites keep passing.
- `bash build/check-format.sh` passes on every changed file.
