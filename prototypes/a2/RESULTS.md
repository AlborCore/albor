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
