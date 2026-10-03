# Prototype A1 phase 1 — two-stage frame on a render thread

Recorded result for phase 1 of Prototype A1 (§30), which measures the two-stage frame of §10.2 (AD-18).
Plan and thresholds: `prototypes/a1/plan.md`. Prototype code: not linked by any runtime module (AGENTS.md).

## Reference machine

Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `release` preset (`-O3`), display 60 Hz at 2.00 backing scale. No content is drawn: the renderer clears and presents, so every number is a lower bound on the real pipeline.

## Measured against the thresholds recorded before the run

| Category | Threshold | Measured | Result |
| --- | --- | --- | --- |
| Scene build on the UI thread, p99 | ≤ 2.0 ms | 0.14 ms (p50 0.05 ms) | pass |
| UI thread blocked by the handoff, p99 | ≤ 1.0 ms | 19.68 ms (p50 16.49 ms) | **fail** |
| Present interval, p99 | ≤ 16.7 ms | 43.24 ms (p50 33.33 ms) | **fail** |
| Submit to present, p99 | ≤ 25.0 ms (R1) | 26.58 ms | **fail** |
| Frames presented while resizing | ≥ 95% of 60 | 60 of 60 | pass |

Pipeline integrity: 580 frames submitted, 580 executed, 0 dropped, 0 coalesced, resize fully presented.

**Baseline for comparison:** Prototype A2, with stage 2 on the UI thread, measured a present interval p50 of 16.67 ms and a p99 of 25.56 ms on the same machine and window.

## Answers to the recorded questions

- **Q1 — does moving render execution to a render thread remove the frame tail? No.** The cadence got worse, not better: p50 went from one display interval (16.67 ms) to two (33.33 ms), and p99 from 25.6 ms to 43.2 ms. The expected improvement did not materialise in this design.
- **Q2 — does the UI thread stay unblocked while the renderer presents? No.** The UI thread is blocked a median of 16.5 ms per frame, almost exactly one display interval, and the p99 is 19.7 ms. The block is not in the queue hand-off (scene build and the envelope push are 0.28 ms and a few microseconds); it is inside `nextDrawable`.

## What actually happens

1. `CAMetalLayer` only hands out a drawable once the compositor has recycled the previous one, and recycling happens on the display cycle. With acquisition on the UI thread and presentation on the render thread, the UI thread waits about one interval for every frame, so each frame costs two intervals instead of one. That is the 33.33 ms median, and it is the direct cause of both failures.
2. The in-flight budget of §10.3 never engages: 0 frames were coalesced and 0 were dropped, because the render thread finishes presenting in 0.29 ms and frees its slot immediately, while the UI thread is then blocked inside the layer. The scheduler cannot coalesce what the layer blocks before the scheduler sees it.
3. So the layer, not the scheduler, is what imposes back-pressure on the UI thread. §10.3's rule "coalesces requests while a frame is in flight instead of blocking the UI thread" cannot be satisfied by a queue alone when the drawable source is synchronous.

## Defects found and fixed while getting here

Four real defects, all caught by running rather than reasoning:

1. **UI thread still acquired a drawable it never used.** Gating acquisition on "does this thread present" was required; before that, the UI thread blocked for an unused drawable every frame.
2. **Waiting for GPU completion serialised presentation.** `waitUntilCompleted` was removed; the compositor should pace presentation.
3. **`nextDrawable` from a background render thread starves.** The render thread blocked for 1 to 8 seconds per frame and executed 4 of 580 frames. Layer ownership has to stay on the thread that drives it.
4. **A drawable pointer copied into a C++ container is not retained.** ARC ownership is not transferred by `std::deque`, so the render thread used a released object and the process died with `EXC_BAD_ACCESS`. The drawable is now retained explicitly and released after presentation and on the drop path.

## Consequences for the plan

- **AD-18 needs restating for macOS before A1.2.** The decision assumed a dedicated render thread is free of cost. On this platform the layer's drawable source is synchronous with presentation, so "render thread owns the surface" is not viable as written. The candidate designs to measure next are (a) a shared drawable where the main thread acquires and presents while only command encoding is off-thread, and (b) a deadline-driven scheduler that decides *when* to present instead of following the display link. Both keep §10.2's rule that stage 2 never calls application code.
- **The frame scheduler needs a layer-aware admission check**, not only a queue: if the drawable source cannot be asked "is one available?", the in-flight budget must be enforced *before* acquisition and must account for the layer's recycle delay, not just for the renderer's progress.
- **A2's tail is now explained differently than first assumed.** A2 presented on the UI thread without waiting for the GPU and still showed a 25.6 ms p99. Since phase 1 shows the layer's recycle cycle dominates when threads are split, the next measurement should instrument A2's own path (acquire time versus encode time) before attributing the tail to GPU submission.
- **Software parity, workloads W1 to W3 and the custom-render API are untouched** and remain A1.2 and A1.3.

## Rerun in release

The phase was rerun with the `release` preset and the table above is the release run; the debug run showed the same shape with a wider tail (p99 56.3 ms), so the conclusion is not an optimisation artefact.

## Still open in this phase

- One window, one machine, one display; W3 multi-window pacing is A1.3.
- The layer is drawn empty, so the cost of real geometry on either thread is unknown.
- Whether the same conclusion holds on Windows and Wayland is unknown and cannot be answered on this machine.
