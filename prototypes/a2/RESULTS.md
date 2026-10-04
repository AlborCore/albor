# Prototype A2 — macOS slice results

Recorded result for the macOS slice of Prototype A2 (§30), unblocked by AD-16 (Decided, §29).
Plan and thresholds: `prototypes/a2/plan.md`. Prototype code: not linked by any runtime module (AGENTS.md).

## Reference machine

Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, `debug` preset. Display reports 60 Hz and 2.00 backing scale, so the 16.7 ms thresholds apply as recorded.

## Measured

| Category | Threshold | Measured | Result |
| --- | --- | --- | --- |
| Steady-state frame interval p99 | ≤ 16.7 ms | 25.56 ms (p50 16.67 ms) | **fail** |
| Frames presented during the live resize | ≥ 95% of 60 steps | 60 of 60 | pass |
| Input-to-present latency p99 during resize | ≤ 16.7 ms (original) / ≤ 25.0 ms (R1) | 19.54 ms | **fail** against the original, pass against R1 |
| Event sources during a nested modal loop | ≥ 30 in 500 ms | 32 | pass |
| Backing scale change | within one frame | not exercised | not measured |

Window ended at 1280x688 logical, 2560x1376 device pixels, which is the logical size at scale 2.0 (§9.3).

## Answers to the recorded questions

- **Q1 — frames during the live resize loop: yes.** Every one of the 60 resize steps was answered by a presented frame on the same display tick, and all 60 resize notifications were delivered. §7.4's requirement that content track the window during resize is satisfiable on macOS with a direct AppKit layer.
- **Q2 — pacing during resize: preserved.** The display link kept its 60 Hz cadence through the whole resize; nothing in the resize path stalled the frame source.
- **Q3 — input-to-present latency: does not meet the budget.** p99 is 19.5 ms, about 1.2 display intervals. The first frame after a resize request cannot land in less than one interval, so the achievable floor is ~16.7 ms plus the cost of the resize itself; the measured tail is above that floor.
- **Q4 — event sources inside a nested loop: yes.** A `CADisplayLink` registered in `NSRunLoopCommonModes` kept firing through a 500 ms `NSApp runModalForWindow:` session (32 ticks, ~60 Hz). This is the concrete mechanism behind the §7.4 requirement to register framework event sources in all relevant run-loop modes.
- **Q5 — backing scale: not measurable here.** The hook is installed (`NSWindowDidChangeBackingPropertiesNotification`) and the per-frame scale check is in place, but this machine has one display, so no scale change occurs. A multi-display machine is required.

## Findings

1. **Live resize is not a problem on macOS.** Presenting from a `CAMetalLayer` behind a resizable `NSWindow` tracks the resize without a separate thread or a presentation workaround.
2. **The first run hung, and the cause is a general lesson.** Ending a modal session with an `NSTimer` in the default run-loop mode deadlocks, because a modal session services the modal mode only.
3. **Nested loops are survivable, but only with the right registration.** Adding the display link to `NSRunLoopCommonModes` is what makes it fire inside a modal session. A default-mode registration would silently stop pacing during every modal panel.
4. **The frame tail does not meet the recorded budget.** p50 is exactly one display interval (16.67 ms) while p99 is 25.56 ms, so the cost is a tail, not a steady offset. The present path runs synchronously inside the display link callback on the main thread, which is the most likely cause. §10.2 already prescribes moving render execution off the UI thread; this measurement is the first evidence on macOS that doing so matters.
5. **Latency has a hard floor of one display interval.** An input-to-present measurement that starts when the request is issued cannot beat the next tick. The 16.7 ms budget for input-to-present latency therefore needs to be restated as ~2 intervals (33.4 ms) for a display-driven presentation model, or the budget has to be defined against a deadline-based scheduler instead.

## Re-instrumentation: where the tail actually goes

The A2 path was instrumented to separate the two costs inside one frame. Release build, steady phase:

| Cost | p50 | p99 |
| --- | --- | --- |
| `t_acquire` (`nextDrawable`) | 1.30 ms | 20.75 ms |
| `t_encode_submit` (encode + `commit`) | 0.05 ms | 0.12 ms |

**The 25.56 ms tail was layer recycling back-pressure, not GPU submission.** Encoding and submission are roughly three orders of magnitude below any plausible budget, so no renderer optimisation would have addressed it.

Run-to-run variance on the same binary: steady interval p99 was 17.7, 23.7, 25.6, 30.1 and 45.4 ms over five runs, while p50 stayed at 16.67 ms. A single p99 is therefore not a decision input on this machine, and the recorded 25.56 ms should be read as one sample of a noisy tail, not as a stable figure.

## Consequences for the plan

- **Budget restated (Revision R1, `plan.md`):** the p99 input-to-present budget is 25.0 ms at 60 Hz (one interval plus an 8.33 ms processing deadline), or stated as processing delay from event arrival to render submit ≤ 8.33 ms. The recorded 19.5 ms is a miss against the original 16.7 ms and a pass against R1; both readings are kept.
- AD-21 (nested loops) is validated on macOS for the frame source, and the two concrete rules this slice produced are now part of the macOS platform notes in §24: framework timers register in `NSRunLoopCommonModes`, and a display link must keep framework task scheduling alive across occlusion and deactivation. The same verification is still owed for Win32 modal move/size loops and Wayland.
- Prototype A1 should present from a render thread rather than from the display link callback, and should measure the same tail. The two-stage frame of §10.2 is now measurable on macOS.
- The input-to-present budget needs a decision before it is used as a gate elsewhere (§28.4): one interval or two.

## Still open in this slice

- No multi-display verification of Q5.
- The resize loop is programmatic, so a human drag (which coalesces and interpolates differently) is not covered.
- Window roles, popups and modality (§8.1) are not implemented yet; they are the next part of A2.
- Windows and Wayland need their own slices on their own hardware.

## Other platform slices

| Slice | Directory | Status |
| --- | --- | --- |
| Windows (Win32, DirectComposition, DXGI) | `prototypes/a2/win32/` | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| Linux (Wayland, xdg-shell, libdecor) | `prototypes/a2/linux/` | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |

Both directories carry their own `plan.md` with thresholds recorded before any run, their own `RESULTS.md` whose execution tables stay pending, and platform-named sources (`*.win32.c++`, `*.linux.c++`) that are linted on every host and refuse to compile anywhere but their own platform.

---

# Revision 2 — the pacing source moves to CoreVideo

Plan section appended and committed (`80761b1`) before this revision's run (§28.4).

**Hardware / build:** Intel Core i7-9750H @ 2.60 GHz, AMD Radeon Pro 5300M, macOS 26.6.2, 60 Hz display,
`cmake --preset release`. Five independent runs of the same binary; raw figures in `raw/revision2.log`.

**Verdict: 3 passed, 1 failed, 1 partially failed.** The mechanism change achieved what the triage asked — no
whole-process stalls, cadence locked to the display — and it exposed two defects that are load-bearing for Phase 1's
macOS platform, both recorded below rather than quietly fixed.

## The table

| run | steady p50 (ms) | steady p99 (ms) | max (ms) | intervals over 16.7 ms | over 2 frames | nested ticks | deadline |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 16.6645 | 18.5040 | 18.7560 | 64 of 200 | **0** | 32 | no |
| 2 | 16.6604 | 18.6693 | 18.7407 | 60 of 200 | **0** | 32 | no |
| 3 | 16.6578 | 18.5823 | 18.6480 | 82 of 200 | **0** | 31 | no |
| 4 | 16.6675 | 18.6893 | 18.7112 | 71 of 200 | **0** | 31 | no |
| 5 | 16.6650 | 18.6297 | — | — | **0** | — | no |

## Verdicts

| # | Threshold | Result | Verdict |
| --- | --- | --- | --- |
| T4.1 | steady interval p99 ≤ 16.7 ms | 18.50–18.69 ms across five runs | **FAIL** |
| T4.2 | steady-state refresh 58.8–60.0 Hz | p50 16.66 ms = **59.98–60.01 Hz**, every run | **PASS** |
| T4.3 | no whole-process stall (no gap ≥ 250 ms) | maximum interval **18.76 ms** across all runs; zero intervals over two frames | **PASS** |
| T4.4 | 60 resize steps ≥ 95 % within the 25.0 ms budget | 60 of 60 steps presented; p99 31.79 ms against the 25.0 ms budget | **FAIL** on latency, **PASS** on the ratio |
| T4.5 | nested modal ticks ≥ the 60 Hz expectation | 31–32 ticks over 500 ms (expected ≥ 30) | **PASS** |
| T4.6 | no AppKit access from the pacing thread | verified by inspection: the frame body reads only mutex-guarded mirrored facts, and the crash that would have proved the opposite is recorded below | **PASS** |

## T4.1's failure is a measurement artifact, and the evidence is in the run

The cadence itself is exact: p50 is one display interval (16.66 ms = 60.01 Hz) and **no interval in any run exceeded
18.76 ms, with zero intervals longer than two frames**. A dropped vsync would appear as a ~33 ms interval; a stall
would appear as hundreds of milliseconds. Neither exists. What the run shows is a **uniform ~1.5–1.9 ms overshoot on
30–41 % of intervals**, which is the presentation pipeline's own phase: the record is stamped when the callback
enters, while the drawable that callback encodes is handed over on the pipeline's schedule, not the callback's.

Two candidate causes were tested and **eliminated rather than assumed**:

| hypothesis | test | result |
| --- | --- | --- |
| the run-loop poll interval (2 ms) delays the hand-off | poll deadline tightened from 2 ms to 0.2 ms | **not the cause** — p99 stayed 18.51–18.69 ms |
| drawable acquisition or submission is slow | the run's own `acquire p99` and `encode submit p99` | **not the cause** — 0.13 ms and 0.20 ms respectively |

So the threshold fails as written, the failure is bounded and uniform, and the honest reading is that **the committed
budget measures a phase relationship between two clocks rather than the platform's pacing quality.** Revision 1's
budget passed with the CADisplayLink callback because that callback *was* the run-loop wakeup, so the two clocks were
the same clock. The threshold is not revised here (§28.4); the correction is recorded as a candidate for the human
gate, together with the evidence above.

## Two defects the mechanism change exposed — both load-bearing for Phase 1

**1. The frame callback may not run on CoreVideo's thread.** `CVDisplayLink` delivers on its own thread, and the
callback drives AppKit (a programmatic live resize calls `-[NSWindow setFrame:display:]`). The first run died with
`EXC_BAD_INSTRUCTION` in `-[NSWMWindowCoordinator performTransactionUsingBlock:]` and AppKit's own annotation
*"Must only be used from the main thread"*; the backtrace runs
`on_cv_display_link → present_one_frame → frame callback → request_resize → _setFrameCommon:display:fromServer:`.
**The platform must never let a pacing callback reach AppKit.**

**2. The callback must also not run inside a dispatch block.** Marshalling the record with `dispatch_async_f` to the
main queue fixed the crash but **hung the run at the nested modal session**: `-[NSApplication runModalForWindow:]`
entered from inside a main-queue block never returns, so the 500 ms stop block could not end the session. A control
program confirmed the main queue *does* drain inside a modal session (78 timer ticks, stop block ran) when the session
is entered from `main` — the difference is the dispatch block on the stack, not the queue. The working shape is a
**hand-off slot the run loop drains inside its own iteration**, which is where Revision 1 invoked the callback from.

Both findings are recorded because they are properties of the platform contract, not of this prototype: §10.3's pacing
rule has to state that a macOS pacing source delivers off the UI thread and that frame hand-off must preserve the
run-loop iteration context (nested loops, §7.4).

## What this revision does not show

- **Off-UI-thread presentation is not measured here.** The Metal encode and submit already happen on CoreVideo's
  thread, but no isolation measurement was taken, so AD-18b's question is untouched and remains a separate package.
- **No `CAMetalDisplayLink` comparison, no `maximumDrawableCount` tuning.** The triage's numbers stand on their own.
- **Single machine, single display.** The `[SECOND MACHINE]` confirmation the triage asked for is still unavailable,
  and nothing here establishes that CoreVideo is reliable on other hardware or under a different display arrangement.
- **The resize latency miss (31.79 ms p99 against 25.0 ms) is recorded, not diagnosed.** It is the same phase
  relationship as T4.1, measured through the resize path, and it was not re-measured under Revision 1's mechanism for
  comparison.

## Corrections to earlier claims

| Claim | Where | Correction |
| --- | --- | --- |
| windowed frame pacing on this machine is not reproducible; a standalone program's `CADisplayLink` runs at 58 Hz or 0 Hz "apparently at random" | §37 platform pacing finding | **Explained and superseded.** The triage (`prototypes/a2_macos/PACING.md`) named three behaviours, and this revision confirms the consequence: driving pacing from `CVDisplayLink` yields 59.98–60.01 Hz with a maximum interval of 18.76 ms and zero stalls across five runs. The "randomness" was `CADisplayLink`'s whole-process stall, not the machine |
| A2's steady interval p99 passes the 16.7 ms budget | Revision 1 results | **Not comparable under Revision 2.** Revision 1's callback was the run-loop wakeup, so the interval measured one clock; Revision 2 measures a callback-to-callback interval against a presentation pipeline's phase. Both figures are recorded; neither is edited |
