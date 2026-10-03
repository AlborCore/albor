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


---

# Phase A1.2a — layer-aware frame scheduling (main thread presents, worker records)

Thresholds were committed to `plan.md` before this run. Same workload as A1.1, so the phases are directly comparable.

## Measured (release preset)

| Category | Threshold | Measured | Result |
| --- | --- | --- | --- |
| Scene build on the UI thread, p99 | ≤ 2.0 ms | 0.06 ms (p50 0.034 ms) | pass |
| Encoding on the worker, p99 | ≤ 8.33 ms | 0.05 ms (present cost p99) | pass |
| Present interval, p99 | ≤ 16.7 ms | 34.62 ms (p50 16.68 ms) | **fail** |
| UI thread blocked, p99 | ≤ 1.0 ms | 33.89 ms (p50 16.59 ms) | **fail** |
| Submit to present, p99 | ≤ 25.0 ms (R1) | 50.87 ms | **fail** |
| Frames presented while resizing | ≥ 95% | 60 of 60 | pass |
| Nested modal dispatches | ≥ 30 in 500 ms | 32 | pass |

Pipeline: 580 frames submitted, 574 executed, 0 dropped, 4 coalesced.

## What changed against A1.1

- **Cadence is fixed at the median.** Present interval p50 went from 33.33 ms (two display intervals) to **16.68 ms**, exactly one interval, by moving `presentDrawable:` back to the UI thread. A1.1's doubled cadence was an artefact of presenting from the background thread.
- **Recording is now genuinely off the UI thread.** Encoding costs 0.05 ms p99 against the 8.33 ms processing deadline, so stage 2 recording has an enormous margin and is not a source of tail.
- **Scene build got cheaper** (0.06 ms p99 against 0.14 ms in A1.1) because the UI thread no longer performs presentation.

## What did not work

The UI thread is still blocked a median of 16.59 ms, which is one display interval, and the tail is unchanged in kind.

**Cause, established by measurement rather than inference:** `CAMetalLayer` returns roughly one drawable per display cycle, and a drawable only returns to the pool after the compositor has *displayed* the frame that used it. A frame acquired at tick N and presented at tick N+1 therefore leaves the pool empty until tick N+1's compositor pass, so the next acquisition waits a full interval. With one drawable becoming available per cycle, no amount of queueing helps: the UI thread cannot both present and acquire in the same tick without waiting for the recycle.

**The missing capability:** coalescing this properly needs a non-blocking "is a drawable available" signal. `CAMetalLayer` does not expose one in this SDK (`allowsNextDrawable` does not exist on macOS here; the selector is unrecognised at runtime), so the in-flight budget of §10.3 can only be enforced *after* acquisition, which is too late to prevent the block.

## Consequences

- **AD-18 R1 is confirmed in part and refuted in part.** Moving recording off the UI thread is right and cheap. "The render thread owns the surface" is refuted for macOS. The unresolved piece is how the UI thread avoids blocking in drawable acquisition, and that is a platform-capability question, not a scheduling question.
- **Candidate designs to measure next:** (a) a deadline-driven scheduler that decides *when* to present rather than following the display link, so acquisition happens once per deadline instead of once per tick; (b) a double-buffered scheme that presents the frame acquired two cycles ago, keeping one drawable always free; (c) `MTKView`, whose `isPaused` and presentation cadence management may expose the availability signal that `CAMetalLayer` hides. All three need a macOS slice of their own before AD-18 R1 can be written.
- **The A2 tail question is answered.** Re-instrumenting A2 split the frame cost: `t_acquire` p50 1.30 ms and p99 20.75 ms, against `t_encode_submit` p50 0.05 ms and p99 0.12 ms. **A2's tail was layer recycling back-pressure, not GPU submission.** GPU submission is three orders of magnitude below the budget, so no amount of renderer optimisation would have fixed it.
- **Run-to-run variance is itself a finding.** A2's steady interval p99 across five runs of the same binary was 17.7, 23.7, 25.6, 30.1 and 45.4 ms. A single p99 from one run is not a decision input on this machine; the median is stable at 16.67 ms and the tail is not. Any future threshold on a tail needs multiple runs and a stated percentile of runs, or a warmed, non-interactive measurement protocol.

---

# Phase A1.3 — Skia workloads W1 to W3 (ganesh on metal)

Thresholds were committed to `plan.md` before the run. Reference machine: Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `release` preset, display at 60 Hz and 2.00 backing scale. Renderer that actually came up: **ganesh-metal**.

Driver: `./.build-release/prototypes/a1/albor-prototype-a1-workloads`. The process reported **10 threshold failures** and exited non-zero, which is the recorded outcome. W1 and W2 completed their frame budgets; **W3 hit its 180 s deadline at 130 of 400 frames, so W3's numbers are not a valid measurement** and are reported as such.

## W1 — waveform, 10^6 cosmetic line segments, continuous pan and zoom

| Category | Threshold | p50 | p95 | p99 | Result |
| --- | --- | --- | --- | --- | --- |
| First retained batch build (10^6 segments) | ≤ 250 ms | 63.37 ms | — | 63.37 ms | pass |
| Append build per 100k segments | ≤ 25 ms | 94.32 ms | 119.93 ms | 119.93 ms | **fail** |
| Recording the frame | ≤ 8.33 ms | 0.18 ms | 0.29 ms | 0.39 ms | pass |
| Recording p95 moved / still (the O(1) claim) | ≤ 1.25× | — | — | 0.976× | pass |
| Recording p99 per segment | ≤ 8 ns | — | — | 0.219 ns | pass |
| Recording on frames that observed a `Snapshot` swap | ≤ 16.7 ms | 0.17 ms | 0.24 ms | 0.24 ms | pass |
| Present interval | ≤ 16.7 ms | 149.74 ms | 279.62 ms | 420.53 ms | **fail** |
| Frames dropped | ≤ 1% | 0 of 401 | — | — | pass |
| Draw calls per frame | exactly 1 | 220 | — | — | **fail** |
| `t_acquire` (diagnostic) | — | 0.0096 ms | 0.0260 ms | 0.0377 ms | — |
| Flush and submit (diagnostic) | — | 149.29 ms | 278.28 ms | 418.10 ms | — |

401 frames offered, 401 drawn, 9 stream publishes, 1.8 × 10^6 segments at the end, 79,215,840 bytes retained in the final set, 554,511,096 bytes allocated across every published set.

## W2 — CAD canvas, 10^5 symbols, 10^6 segments, 4000 labels

| Category | Threshold | p50 | p95 | p99 | Result |
| --- | --- | --- | --- | --- | --- |
| Retained bytes | ≤ 64 MB | 47,820,898 | — | — | pass |
| Recording the frame | ≤ 8.33 ms | 47.48 ms | 51.00 ms | 53.77 ms | **fail** |
| Hover hit query on the UI thread | ≤ 1.0 ms | 0.0010 ms | 0.0021 ms | 0.0037 ms | pass |
| Queries that hit the expected symbol | ≥ 99% | 99.41% | — | — | pass |
| Present interval | ≤ 16.7 ms | 153.40 ms | 201.21 ms | 240.65 ms | **fail** |
| `t_acquire` (diagnostic) | — | 0.0144 ms | 0.0337 ms | 0.0558 ms | — |

3208 hover queries over 401 frames.

## W3 — multi-window isolation, 100 ms presentation hold on window 1

**Not a valid measurement.** The run reached 130 of 400 frames before the 180 s deadline, so the hold window ran 146 seconds rather than the intended few seconds, and window 2's cadence is a measure of a GPU that was already saturated before the hold began.

| Category | Threshold | Measured | Result |
| --- | --- | --- | --- |
| Window 1 frames dropped | ≤ 1% | 2 of 130 (1.54%) | fail (deadline-limited) |
| Window 2 hover queries during the hold | ≥ 30 | 29 | fail (deadline-limited) |
| Window 2 frames presented during the hold | ≥ 95% of offered | 0.33% | fail (deadline-limited) |
| Window 2 present interval, whole run | ≤ 16.7 ms | p50 442 ms | fail (deadline-limited) |
| Window 2 present interval during the hold | ≤ 16.7 ms | p50 1244 ms | fail (deadline-limited) |

## What the measurements establish

**Ganesh strokes lines on the CPU, and this is the single most consequential result of the phase.** A
retained batch drawn with `SkCanvas::drawPoints` and a stroke paint costs **10,470 ms per frame** (p50, 1.8 × 10^6
segments). A stack sample taken inside the frame shows the whole cost:

```text
SkCanvas::onDrawPoints
  skgpu::ganesh::Device::drawPoints
    skcpu::Draw::drawDevicePoints          <-- the cpu path
      skgpu::ganesh::Device::drawPath
        skgpu::ganesh::TessellationPathRenderer::onDrawPath
          skgpu::ganesh::StrokeTessellateOp
```

Ganesh has no GPU line path: a stroked primitive is tessellated on the CPU every frame. The same geometry expressed as
**pre-tessellated filled quads** through `SkVertices` costs **0.18 ms p50** — about 55,000× less. §11.2's retained
line batch therefore has to be filled quads, not strokes, or the O(1) promise is worthless. This is a decision input
for §11.2 and for the `render-skia` module, not a tuning detail.

**The retained-draw promise holds for what it actually promises.** Pan and zoom never rebuild geometry: recording p95
with the view moving is 0.976× the p95 with the view still, well inside the 1.25× threshold, and the marginal cost is
0.219 ns per segment. The append stream is invisible to the frame that observes it (0.24 ms p99 against 16.7 ms).

**But "O(1)" cannot mean "one draw call" at this scale, and the pre-registered threshold of exactly 1 is refuted.**
Skia indexes a vertex batch with 16 bits, so one draw call cannot exceed 65,535 vertices; at four vertices per line
quad that is 8192 segments per call, hence **220 draw calls** for 1.8 × 10^6 segments. A single `drawPoints` call over
the whole batch trips a Skia check (`SkTFitsIn`) above ~32,768 points. §11.2's structural claim has to be restated as
"a fixed number of draw calls per frame, independent of the pan", which is what the 0.976× result supports.

**The GPU, not the framework, is the limiter.** `t_acquire` is 0.0096 ms at p50 in W1 and the whole recording is
0.18 ms; the flush and submit that follows is 149 ms. A display-driven present cannot beat the GPU, so the 149 ms
present interval and the 153 ms one in W2 are the GPU saturating on 1.8 × 10^6 filled quads at 2560 × 1440 on an
Intel integrated GPU. No amount of framework work moves this number.

**Appending to a retained batch is O(n), not amortised.** Rebuilding a 1.8 × 10^6 segment batch takes 94 ms at p50 and
120 ms at p99 against a 25 ms threshold. The prototype rebuilds and republishes the whole batch; a real append stream
needs a growable buffer or a chunked set so that an append costs the delta, not the history.

**Run-to-run variance is large enough to invalidate a single tail sample.** Across runs of the same binary on this
machine, W2's recording p50 was 5.92 ms, 6.16 ms, 47.13 ms and 47.48 ms. W1's present interval p50 was 61 ms, 89 ms,
95 ms and 150 ms. Any threshold on a tail needs multiple runs and a stated percentile of runs, as A2 already found.

## Defects found and fixed in this phase

- **`CAMetalLayer` drawables are BGRA8.** Declaring `kRGBA_8888_SkColorType` makes Ganesh reject the colour format
  (`Illegal color type (5) and format (80) combination`) and silently fall back, which cost 10.5 s per frame before it
  was found.
- **libc++ has no `std::atomic<std::shared_ptr>`** (P0718R2 is unimplemented), so `Snapshot<T>` guards its swap with a
  small spin lock instead. The lock is released before any deleter runs.
- **A display link outlives the loop that drove it.** A workload's callback captured locals by reference; after the loop
  ended the link still fired and reached a destroyed `GpuContext`, which segfaulted. `PlatformWindow` now has
  `detach_frame_source()`, and every workload owns its frame state through a `shared_ptr` captured by value.
- **A hit query missed symbols near a cell boundary.** Symbols are filed under the cell of their centre, so a pointer
  one cell away found nothing; the query now walks the 3 × 3 neighbourhood and the hit ratio rose from 0.4375 to 0.9941.
- **AppKit cannot create a window after its run loop has stopped** (a hang inside `NSThemeFrame initWithFrame:`,
  confirmed with `sample`), so all windows are created before the first loop and deliberately never destroyed.
- **The vcpkg port installs Skia debug-only**, so the `release` preset links a debug Skia and Skia's internal checks are
  live. That is lucky: the 16-bit index limit surfaced as a loud check failure instead of silent corruption.

## What the phase did not answer

- **Q5 (Ganesh versus Graphite) stays open.** This phase measured Ganesh, but not because Skia 148 lacks Graphite:
  `vcpkg.json` requests only the `metal` feature, and the port's `graphite` feature was never enabled. The headers
  `gpu/graphite/mtl/MtlBackendContext.h` are installed unconditionally, which is what made the missing symbols look
  like a missing capability rather than an unrequested one. Enabling it needs `vulkan-headers`,
  `vulkan-memory-allocator` and a Skia rebuild, and is queued as a dependency change. The same correction applies to
  HarfBuzz and `skparagraph`, which are absent for the same reason: their port features were not requested. Until that
  slice runs, the frame pipeline must assume Ganesh threading rules, and nothing here says Graphite would be worse.
- **Q6 (software renderer parity) was not run**: this phase measured the GPU path only.
- **W3's isolation question is unresolved.** At 10^6 segments both windows are GPU-bound, so a 100 ms hold on window 1 is
  invisible in window 2's cadence and the experiment cannot discriminate. W3 needs a segment count this GPU sustains at
  60 Hz — the next slice should sweep 10^4, 10^5 and 10^6 and run the isolation test at the largest passing scale.
- **The two-stage frame is not used here.** Recording and submission both happen on the UI thread. A1.2a already showed a
  CPU recording worker costs 0.46 ms p99, so the open question is whether Ganesh's single-owner context can be driven
  from a worker while the UI thread wraps the surface.
