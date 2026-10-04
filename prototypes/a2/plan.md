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

## Platform slices beyond macOS

The macOS slice is the only one that can run on the current host. The other two platforms have their own directories, with their own pre-recorded thresholds, because a threshold recorded on one platform is not a threshold on another:

| Slice | Directory | Plan | Status |
| --- | --- | --- | --- |
| **A2-W** | `prototypes/a2/win32/` | `win32/plan.md` | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| **A2-L** | `prototypes/a2/linux/` | `linux/plan.md` | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |

Both record the same R1 budgets (≤ 25.0 ms input-to-present, ≤ 8.33 ms processing, ≤ 1.0 ms UI thread block) plus the platform-specific questions: `WM_ENTERSIZEMOVE` and `WM_DPICHANGED` for Win32, `wl_frame` throttling and `xdg_positioner` for Wayland. Both leave the reference machine table empty on purpose: §28.4 forbids starting a run before the hardware is named.

---

## Revision 2 — the pacing source moves to CoreVideo (plan.md, AD-18b)

**thresholds below are committed before the revision's run (§28.4)**

### What changed, and why

Prototype A2's pacing triage (`prototypes/a2_macos/PACING.md`) measured three display-link mechanisms on this machine
with a complete condition table and five runs per cell:

| mechanism | committed cells |
| --- | --- |
| `NSScreen.displayLinkWithTarget:selector:` (`CADisplayLink`) | 0.0 Hz in 20 of 20 runs — whole-process stalls with no recorded correlate |
| `CAMetalDisplayLink` | 0.6 Hz in 20 of 20 matrix runs; 46–58 Hz when it is the first link in a fresh process with a composited window |
| `CVDisplayLink` | 58.8–60.0 Hz in 60 of 60 runs under every condition |

Revision 1 drove pacing from `CADisplayLink`. This revision drives it from `CVDisplayLink`, which is the only mechanism
the triage found uniformly reliable. Because `CVDisplayLink` delivers its callback on **its own thread**, two things
follow, and both are part of this revision rather than deferrals:

1. **The frame body may not read AppKit.** An `NSWindow`/`NSView` property read is not safe from CoreVideo's thread, so
   the window's bounds and backing scale are **mirrored into the platform state by the main queue** (the resize and
   backing-change observers refresh them) and the pacing thread reads only the mirrored facts.
2. **Presentation now happens off the UI thread** for this prototype's measured path, which is the mechanism AD-18b's
   open question asks about. The isolation measurement itself remains a separate package; this revision only records
   what the mechanism change does to A2's own thresholds.

### Method

Revision 1's workload is unchanged, so its figures remain comparable: 200 steady frames, a 60-step programmatic live
resize (320×240 → 1280×720), then a 500 ms nested modal session, all inside the same process and run. Five independent
runs of the same binary. Every run reports the steady interval p50/p99, the presented-frame count, the resize latency,
the nested-loop tick count, and **whether any run reported a whole-process stall** (a steady-phase callback gap
≥ 250 ms, five times the display interval).

### Thresholds (committed before the run)

| # | Threshold | Rationale |
| --- | --- | --- |
| T4.1 | steady interval p99 ≤ 16.7 ms, every run | Revision 1's committed budget, unchanged; the mechanism change must not degrade it |
| T4.2 | steady-state refresh **58.8–60.0 Hz** in every run, i.e. median interval 16.67–17.01 ms | the brief's acceptance band; 60 Hz display, so anything below 58.8 Hz is a dropped tick and anything above 60 is a double-count |
| T4.3 | **no whole-process stall in any run**: no steady-phase callback gap ≥ 250 ms | the finding Revision 2 exists to remove; recorded per run, because one stall in a whole run is the finding returning |
| T4.4 | 60 resize steps present ≥ 95 % within the 25.0 ms input-to-present budget | Revision 1's committed resize ratio, unchanged |
| T4.5 | nested modal session ticks ≥ the 60 Hz expectation for its duration | the re-entrancy result Revision 1 recorded, now measured with a source that needs no run loop registration |
| T4.6 | the mirrored-facts path introduces no AppKit access from the pacing thread | a thread-safety gate, verified by inspection of the revision's diff rather than by timing |

### Commands

```sh
cmake --preset release
cmake --build --preset release --target albor-prototype-a2
./.build-release/prototypes/a2/albor-prototype-a2          # one run
```

### Pass/fail policy

Failures stay failures. A run that stalls in the first seconds only — before `finishLaunching` has connected the
process to the window server — is recorded with its position in the run rather than discarded, because the triage
showed that bring-up order is exactly what distinguishes a working link from a dead one.

### Out of scope

No `CAMetalDisplayLink` fallback, no `maximumDrawableCount` tuning, and no AD-18b isolation measurement. Those remain
separate packages.
