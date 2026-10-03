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

## A1.2b — blocked on the pinned Skia binaries

`RenderContext`, `CustomRenderNode`, the retained resource factory and workloads W1 to W3 are unchanged, but they cannot start until pinned pre-built Skia binaries are available. AD-19 requires Skia as a pinned dependency, and routine test cycles must not build it from source, so this phase stays parked rather than silently switching to a source build.

## Known limits of A1.1

- No content is drawn: the renderer clears and presents, so the frame cost is a lower bound.
- One window only; W3 multi-window pacing is A1.3.
- Skia is not involved yet, so nothing here says anything about Ganesh versus Graphite.
