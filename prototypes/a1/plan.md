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

## Known limits of A1.1

- No content is drawn: the renderer clears and presents, so the frame cost is a lower bound.
- One window only; W3 multi-window pacing is A1.3.
- Skia is not involved yet, so nothing here says anything about Ganesh versus Graphite.
