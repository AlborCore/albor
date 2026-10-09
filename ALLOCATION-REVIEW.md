# Allocation Review: the per frame cost of the framework

Draft review. Every item names the code it comes from, so each can be confirmed or dismissed without
re-reading the tree. The work this review fed is tracked on GitHub: the ordered display list landed, and the partial layout follow-up is open there.

## Summary

Nothing here is a correctness defect, and nothing here is a leak. What the tree does is allocate
per frame in proportion to what it draws, which is the shape of cost a framework pays once per
frame forever. Three of the items are per frame, two are per text line per frame, and one of those
is mine.

The more important finding is that **there is no way to measure any of it**. The tree has no
allocation counter, no benchmark harness under `tools/`, and no steady state frame test, so
"allocates a lot" is currently a judgement rather than a number. Making it measurable is item P0.

## Per frame, proportional to the tree

**A1. The recorder's buffers are built and thrown away every frame.**
`SceneBuilder::build` constructs a `SceneRecorder` per build (`ui/scene_builder.cpp:154`), so its
`m_commands` and its save stack start at zero capacity and grow by doubling, reallocating and
copying every command as they go. This is Phase A item 6 of the ordered display list work, which is
outstanding rather than deferred.

**A2. Every command is copied once more when the scene is frozen.**
`SceneRecorder::scene()` loops the recorded commands and hands each to
`FrameScene::add_command` by value (`ui/scene_builder.cpp:112-114`), so every command is copied a
second time per frame. For a shape or a run that copy is cheap, and for a retained kind it is three
shared pointer reference count bumps. Fixing A1 and A2 together means the recorder's buffer is
*moved* into the scene, which costs one allocation per frame instead of one plus n copies.

**A3. The binding engine allocates its walk order on every settle.**
`BindingEngine::settle` declares `std::vector<BindingId> order` inside the function
(`runtime/binding_engine.cpp:186`), so the vector is constructed, grown to the number of dirty
bindings, and destroyed once per settle, which is once per frame. `order.clear()` reuses capacity
across the passes inside one settle, but not across settles. The engine already owns ui thread
state, so the vector belongs to it beside the rest.

**A4. The tree walk is type erased.** `UiNode::visit` takes a `std::function`
(`ui/ui_node.h:108`), and `SceneBuilder::build` passes a capturing lambda per build
(`ui/scene_builder.cpp:163`). One construction per frame, which may allocate a closure because the
capture is two values. Templating the visitor removes the indirection entirely.

**A5. The frame hand off allocates a queue node.** `RenderExecutor::submit` pushes a `FrameRequest`
into a `std::deque` (`render/render_executor.cpp:40`), which allocates when the deque grows. The
queue holds at most one frame, so this is bounded and small, but it is per frame.

## Per text line, per frame

**B1. The draw path allocated two vectors per shaped line.** `SkiaRenderContext::draw_text` built a
`std::vector<SkGlyphID>` and a `std::vector<SkPoint>` for every line of every run, every frame. This
was mine, introduced with the shaped run work, and it scaled with the amount of text on screen.
**Fixed**: the two buffers are thread local scratch, emptied and refilled per line, which keeps their
capacity across frames. They are per thread rather than members because the context is rebuilt every
frame and a member would die with it, while one surface is drawn on one thread at a time, so a shared
buffer would race between two contexts. Measured: a repeated draw of a wrapped run now costs **0**
allocations, against one vector pair per line before (§6.3).

**B2. Every measure call built a string for the cache key.** The cache was keyed on an owning
`CacheKey`, so a lookup constructed a `std::string` first. Measured, that cost **nothing** for a
short run, because a run of twenty two characters or fewer lands in the small string buffer, and it
cost one heap allocation per call for anything longer. A label is usually short and a paragraph is
not, so the item was real but smaller than it looked. **Fixed**: the cache now takes a view key
through a transparent hash and comparison, and a lookup allocates nothing however long the run is
(`text/text-skia/skia_text_engine.cpp`). Measured after the fix: ten hits of a 200 character run
cost **0** allocations, where they cost 10 before.

## Not problems, checked so nobody reopens them

- `Paint`, `Color`, `Point`, `Rect` and `Transform` travel by value and are trivially copyable.
- A `DrawCommand` for a shape or a run allocates nothing; a retained kind copies three shared
  pointers, which is a reference count and not an allocation.
- A `TextLayout` is a shared pointer and three floats, so copying a measurement is free.
- The `UiNode` tree allocates when a child is added, not when a frame is built.
- The shaped run cache is bounded at 512 entries. It is bounded by entry count rather than by
  bytes, so a cache of very long runs would hold more memory than a cache of short ones.

## P0: make it measurable before changing anything

The ordering here matters. Every proposal below is an optimisation whose benefit is a guess, and two
of them trade an allocation against a copy.

**This part is done.** `tools/alloc-probe` replaces the global operators in a library only the test
binary links, so a shipping build keeps the compiler ones and the probe is excluded at link time
rather than behind a flag (AD-04, §2.3). Counting is off by default, so an ordinary test pays one
relaxed atomic load. It reports both the number of allocations and how many of them are still live,
which makes a leak check a side effect rather than a project.

### What the probe says today

| measurement | allocations |
| --- | --- |
| a scene build of a small tree, first time | 4 |
| the same build again, nothing changed | 4 |
| shaping one run, a cache miss | 429 |
| ten cache hits, short text | 0 |
| ten cache hits, 200 character text | 0 (was 10 before B2) |
| ten distinct runs | 79 |

Two things follow. The frame path of a small tree is **not** where the cost is, which is worth
knowing before optimising it: four allocations for a build and a command is not a problem, and A1
and A2 matter only as the command count grows. The miss cost of 429 is the real cliff, and the
cache is what stands between a tree full of labels and paying it every frame.

The steady state frame test and the two cache tests in `tests/allocation.test.cpp` turn these rows
into bounds a regression can fail.

## Proposed order

1. P0, the three measurements, because the rest is guesswork without them.
2. B1 and B2, **both done**: a repeated draw of a shaped run allocates nothing, and a cache hit
   allocates nothing however long the run is.
3. A1 and A2, only once the command counts justify them. The measurement above says a small tree
   builds in four allocations, so this is not the next thing to spend effort on.
4. A3 and A4, the engine's walk order and the type erased visitor, both small.
5. Re-measure, and only then decide whether the frame path needs a buffer pool, which is the one
   structural change this review does not yet recommend.