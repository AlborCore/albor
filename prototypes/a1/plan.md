# Prototype A1 — Renderer and custom rendering

Recorded plan for Prototype A1 (§30), the largest Phase 0 item. Prototype code: not linked by any runtime module (AGENTS.md).

## Questions

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Does moving render execution to a dedicated render thread remove the frame tail that Prototype A2 measured? | §10.2, AD-18 |
| Q2 | Does the UI thread stay unblocked while the renderer presents? | §10.2 |
| Q3 | What is the custom-render API shape for retained geometry and batched primitives? | §11.1, §11.2, AD-06 |
| Q4 | How are surfaces and GPU devices owned across windows? | §9.2 |
| Q5 | Which Skia backend, Ganesh or Graphite, and what are its threading consequences? | AD-06, open question 7 |
| Q6 | Does the software renderer reproduce every Level 2 resource and primitive? | §11.7, AD-26 |

## Phases

Prototype A1 is too large to run as one experiment, so it is split. Each phase records its own thresholds before it runs.

| Phase | Scope | Status |
| --- | --- | --- |
| **A1.1** | Two-stage frame on the A2 macOS window: UI thread builds an immutable `FrameScene`, a render thread executes and presents it. Answers Q1 and Q2 without needing the renderer yet. | this phase |
| **A1.2** | `RenderContext`, `CustomRenderNode`, retained resources and batched primitives on Skia. Answers Q3 and Q5. | after A1.1 |
| **A1.3** | Workloads W1 (waveform), W2 (CAD-like) and W3 (multi-window pacing) plus software parity. Answers Q4 and Q6. | after A1.2 |

A1.1 deliberately reuses the Prototype A2 macOS window rather than writing a second platform layer, and it presents a cleared frame rather than drawing content. What it measures is the pipeline, not the renderer.

## A1.1 thresholds, recorded before the run

| Category | Threshold |
| --- | --- |
| Steady frame interval p99 | ≤ 16.7 ms |
| UI thread blocked by presentation, p99 | ≤ 1.0 ms |
| Scene build (UI thread), p99 | ≤ 2.0 ms |
| Submit to present p99 | ≤ 25.0 ms (Revision R1 of the A2 input-to-present budget) |
| Frames presented while resizing | ≥ 95% of the resize steps |

**Hardware / build:** the same reference machine as Prototype A2: Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `release` preset, display at 60 Hz and 2.00 backing scale.

**Workload:** 400 steady frames, then a 60-step live resize, then a 500 ms nested modal session, with the render thread running throughout. The scene carries 4096 primitive records so that the handoff is a realistic copy rather than an empty frame.

**Baseline to beat:** Prototype A2 measured a steady interval p99 of 25.56 ms with presentation on the UI thread.

## A1.2a — layer-aware frame scheduling, thresholds recorded before the run

A1.1 showed that the layer, not the scheduler, applies back pressure: the UI thread blocked inside `nextDrawable` and cadence doubled to two display intervals. A1.2a tests the correction without Skia, so the dependency on the pinned Skia binaries does not block the scheduling question.

Design under test, per AD-18 R1 as restated by this prototype:
- `nextDrawable` acquisition and `presentDrawable:` stay pinned to the UI thread inside the `CADisplayLink` tick.
- Command encoding runs on a background worker, so stage 2 never makes the UI thread wait for recording.
- The frame scheduler applies a **layer-aware admission check**: at most `MAX_IN_FLIGHT_DRAWABLES` drawables may be pending, and while the budget is spent the UI thread coalesces the request instead of asking the layer for a drawable (§10.3).

| Category | Threshold |
| --- | --- |
| Present interval p99 | ≤ 16.7 ms |
| UI thread blocked by the handoff, p99 | ≤ 1.0 ms |
| Scene build on the UI thread, p99 | ≤ 2.0 ms |
| Encoding on the worker, p99 | ≤ 8.33 ms (the R1 processing deadline) |
| Input-to-present p99 | ≤ 25.0 ms (Revision R1) |
| Frames presented while resizing | ≥ 95% of the resize steps |
| Nested modal session dispatches | ≥ 30 in 500 ms |

**Workload:** unchanged from A1.1 (400 steady frames, 60 resize steps, 500 ms nested modal session, 4096 primitive records per scene), so the two phases are directly comparable.

**Baseline to beat:** A1.1 measured a present interval p50 of 33.33 ms and a UI thread block p50 of 16.49 ms. A2, presenting on the UI thread, measured 16.67 ms p50.

**Also measured, as a diagnostic rather than a gate:** `t_acquire` (the `nextDrawable` call) against `t_encode_submit` (command encoding plus `commit`) in the A2 path, to establish whether A2's 25.56 ms tail came from layer recycling or from GPU submission.

## A1.3 — Skia workloads W1 to W3, thresholds recorded before the run

A1.2b was parked waiting for the renderer. The dependency is now resolved: `vcpkg.json` pins the `skia` port for each
platform, and the vcpkg installed tree provides `unofficial::skia::skia` as a CMake target. Milestone 148 is the
version in that tree.

**Two different things have to be separated here: what Skia milestone 148 *is*, and what our dependency
configuration actually *built*. Conflating them is how this section first got a wrong answer.**

*What milestone 148 is — genuine API facts, read out of the installed headers:*

| Fact | Consequence for the prototype |
| --- | --- |
| `SkSurfaces::WrapMetal` no longer exists | the surface factory is `gpu/ganesh/mtl/SkSurfaceMetal.h`'s `WrapCAMetalLayer` |
| `SkCanvas::drawText` is gone | text is drawn with `drawSimpleText` / `drawString` |
| `SkPictureRecorder::endRecordingAsPicture` is gone | renamed to `finishRecordingAsPicture` |
| `SkFont` is no longer reference counted | held by value, not in an `sk_sp` |
| `SkFontMgr::RefDefault`, `SkTypeface::MakeFromName/MakeDefault` are gone | fonts resolve through `SkFontMgr_New_CoreText` on macOS |
| `SkPicture::performanceCount`, `SkRTreeFactory` are gone | no picture stats, no `SkRTreeFactory`; `beginRecording` takes `nullptr` |
| `SkCanvas::drawPoints` takes an `SkSpan` and there is one overload | no legacy `SkScalar*` + count form |
| `SkCanvas::drawVertices` lost `colorsMode`/`texsMode` | presence is inferred from the `SkVertices::Builder` flags |

*What our configuration built — a property of `vcpkg.json`, not of Skia:*

| Capability | In the installed archive | Why |
| --- | --- | --- |
| Ganesh on Metal | **present** | `vcpkg.json` requests the `metal` feature |
| Graphite | absent | the port has a `graphite` feature and we did **not** request it |
| HarfBuzz shaping, `skparagraph` | absent | the port has a `harfbuzz` feature and we did **not** request it |

Headers are installed unconditionally by the port, so `gpu/graphite/mtl/MtlBackendContext.h` and
`modules/skparagraph/` are on disk while the matching symbols are not in `libskia.a`. **Reading those headers as proof
that Graphite is unavailable in milestone 148 was wrong**: the port ships `002-graphite.patch` and maps the `graphite`
feature to `skia_enable_graphite=true`, so Graphite is available and simply unrequested.

This phase therefore measures **Ganesh on Metal**, which is what the current dependency builds. **Q5 (Ganesh versus
Graphite) stays open.** Enabling `graphite` on macOS costs `vulkan-headers` and `vulkan-memory-allocator` (the port's
self-dependency, so Graphite has a backend to link) plus a full Skia rebuild; that is a dependency change, not a
renderer change, and it is queued as a separate slice.

**W1 — waveform, 10^6 cosmetic line segments.** One retained batch built by `ResourceFactory::create_line_batch` on a
background worker, replayed with a single `drawPoints` call while the view pans and zooms continuously. An append stream
publishes growing batches through an atomic `Snapshot<T>` swap.

| Category | Threshold |
| --- | --- |
| Retained batch build on the worker, first 10^6 segments | ≤ 250 ms |
| Append build on the worker, per 100k added segments | ≤ 25 ms |
| Recording the frame, p99 | ≤ 8.33 ms (the R1 processing deadline) |
| Recording on frames where the view transform changed, p95 | ≤ 1.25 × recording p95 on frames where it did not (this is the O(1) claim of §11.2) |
| Recording cost per segment, p99 | ≤ 8 ns |
| Recording on frames that observed a `Snapshot` swap, p99 | ≤ 16.7 ms |
| Draw calls per frame | exactly 1 |
| Present interval, p99 | ≤ 16.7 ms |
| Frames dropped | ≤ 1% of the frames the display offered |

**W2 — CAD canvas, 10^5 symbols, ~10^6 segments, 4000 labels.** Retained geometry, a retained uniform grid over the
symbols, and one shared label buffer. Every frame issues 8 hover queries at symbol centres while the view pans and zooms.

| Category | Threshold |
| --- | --- |
| Recording the frame, p99 | ≤ 8.33 ms |
| Hover hit query on the UI thread, p99 | ≤ 1.0 ms |
| Queries that hit the expected symbol | ≥ 99% (a miss is a grid or transform bug, not a workload fact) |
| Present interval, p99 | ≤ 16.7 ms |
| Retained bytes for the whole set | ≤ 64 MB |

**W3 — multi-window isolation.** Two windows, each replaying W1 from one shared immutable retained set. Window 1 holds
its presenting thread for 100 ms after each submit, which is the injected GPU presentation stall.

| Category | Threshold |
| --- | --- |
| Window 2 frames presented during the hold | ≥ 95% of the frames the display offered |
| Window 2 interval during the hold, p99 | ≤ 16.7 ms |
| Window 2 hover queries answered during the hold | ≥ 30 |
| Window 1 frames presented | ≥ 95% of its own attempts |

**Hardware / build:** the same reference machine as A1.2a — Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2,
`release` preset, display at 60 Hz and 2.00 backing scale.

**Workload:** 400 frames per workload. W1 and W2 run single window; W3 runs both windows until window 1 has presented 400
frames. The run reports p50, p95 and p99 for every timing, because a single tail sample is not a decision input on this
machine (see the A2 variance finding).

**Recorded limitation:** this phase records and submits on the UI thread inside the display tick. A1.2a already showed
that a CPU recording worker is cheap (0.46 ms p99 against 8.33 ms), so the two-stage shape is not in question here; what
is in question is whether Ganesh's single-owner context can be driven from a worker while the UI thread wraps the
surface. That split is left open rather than guessed.

## A1.2b — resolved, and folded into A1.3

`RenderContext`, `CustomRenderNode`, the retained resource factory and workloads W1 to W3 are unchanged. The blocker was
the absence of a pinned Skia build; `vcpkg.json` now pins the port per platform and the installed tree provides a CMake
target, so the phase runs as **A1.3** above rather than as a separate step.

## Known limits of A1.1

- No content is drawn: the renderer clears and presents, so the frame cost is a lower bound.
- One window only; W3 multi-window pacing is A1.3.
- Skia is not involved yet, so nothing here says anything about Ganesh versus Graphite.
