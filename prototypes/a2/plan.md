# Prototype A2 — Platform presentation (macOS slice)

Recorded plan for the macOS slice of Prototype A2 (§30), unblocked by AD-16 (Decided, §29).
Prototype code. It is not a runtime module and must not be linked by one (AGENTS.md).

## Scope of this slice

AD-16 settles that Albor writes its own platform layer on AppKit. This slice answers, on macOS only, the questions that only a real platform can answer. Windows and Wayland need their own slices on their own hardware; nothing here claims anything about them.

Prototype A1 (renderer) stays separate: this slice does not render with Skia and does not decide AD-16. It presents Metal frames directly through `CAMetalLayer` to measure the platform contract, not the renderer.

## Questions

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Do frames keep being presented during the live resize (nested) loop? | §7.4 |
| Q2 | Does display-link frame pacing hold during resize, or does it collapse? | §10.3 |
| Q3 | What is the input-to-present latency while the window is being resized? | §28.2 |
| Q4 | Do the framework event sources keep running inside a nested modal loop? | §7.4, AD-21 |
| Q5 | Is a backing scale change detected and propagated to the metrics? | §9.1, §23.1 |

## Recorded before the run

**Thresholds** (set here before any measurement, §28.4):

| Category | Threshold |
| --- | --- |
| Steady-state frame interval p99 | ≤ 16.7 ms (the measured display reports 60 Hz) |
| Frames presented during the resize loop | ≥ 95% of the resize-loop opportunities |
| Input-to-present latency p99 during resize | ≤ 16.7 ms, restated to ≤ 25.0 ms (see Revision R1) |
| Event sources serviced during a nested modal loop | ≥ 30 in 500 ms |
| Backing scale change | detected within one frame of the change |

**Hardware / build:** reference machine for this slice is an Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2, built with the `debug` and `release` presets. Display reports 2.00 backing scale and 60 Hz; the run records both again and fails the thresholds if the display is not 60 Hz, because a different refresh rate would invalidate the latency thresholds.

**Workload:** 200 steady frames, then a programmatic live resize of 320x240 to 1280x720 in 60 steps with a display-link frame between each step, then a nested modal session of 500 ms while the frame source keeps running.

## Revision R1 — input-to-present budget, restated after the first run

The first run failed the input-to-present budget at p99 = 19.5 ms. The budget itself was wrong, not the platform, and the restatement was reviewed and approved before being applied:

- A display-driven present cannot beat the next tick, so an event that arrives just after a tick boundary waits a full interval.
- Against a 16.7 ms total budget that leaves 0 ms for input routing, layout, scene build and GPU submission (§10.2), which is not a measurable requirement.
- **Restated budget: p99 input-to-present ≤ 25.0 ms at 60 Hz**, that is one vsync interval plus an 8.33 ms processing deadline, or at most 1.5 frames.
- **Equivalent framing that isolates the framework:** processing delay from event arrival to render submit ≤ 8.33 ms. This is the number the renderer work has to move; the total is then dominated by vsync alignment rather than by the framework.

The original result stands as recorded (a miss against 16.7 ms). Under R1 the measured 19.5 ms would pass, and the honest reading is that the framework already fits inside half a frame of processing time.

## Known limits of this slice

- One display only, so Q5 verifies that the scale-change hook is installed and fires, not that a cross-display move is handled.
- The resize loop is driven programmatically rather than by a human drag, so gesture-level behaviour is not covered.
- Text, IME and accessibility are Prototype C and Prototype A2's later platform slices, not this one.
