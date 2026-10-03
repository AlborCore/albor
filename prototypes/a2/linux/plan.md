# Prototype A2 — Linux slice (Wayland / `xdg_shell` / `libdecor`)

Pre-recorded plan for the Linux platform slice of Prototype A2 (§30, AD-16, AD-18 R1). This slice is **not yet executed**: the recording host is a macOS machine with no Wayland compositor, so the implementation files here are scaffolds and the measurement tables stay `PENDING_CI_MATRIX_RUN`. Nothing below may be edited after a run; a change of threshold requires a new revision and a new run, exactly like A2 Revision R1.

Prototype code: not linked by any runtime module (AGENTS.md).

## Question

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Does `wl_frame` request a callback per refresh, and can the prototype coalesce a burst into one frame? | §10.3, AD-18 R1 |
| Q2 | Does `libdecor` client side decoration attach without a visible stall, and does `xdg_toplevel.configure` resize cleanly? | §7.4, §8 |
| Q3 | How expensive is `xdg_positioner` popup creation, which is the Wayland counterpart of the modal session? | §7.4, §22 |
| Q4 | Does `wl_surface.set_buffer_scale` land before the next buffer is attached, so the frame carries the right metrics? | §9.1 |
| Q5 | Does the Wayland round trip cost make the input-to-present budget tighter than the macOS slice? | §9.2, R1 |

## Thresholds, recorded before the run

| Category | Threshold |
| --- | --- |
| Present interval p99 | ≤ 16.7 ms at a 60 Hz reference output |
| UI thread blocked by the `wl_display` round trip or by `wl_frame` back pressure, p99 | ≤ 1.0 ms |
| Scene build on the UI thread, p99 | ≤ 2.0 ms |
| Recording and submitting a frame, p99 | ≤ 8.33 ms (the R1 processing deadline) |
| Input-to-present p99 | ≤ 25.0 ms (Revision R1 of the A2 budget) |
| Frames presented while a live resize is in flight | ≥ 95% of the resize steps |
| `wl_frame` callbacks honoured | ≤ 1 per refresh period, 100% of the time |
| Callback burst coalescing | ≥ 95% of burst requests turn into a single frame |
| Wayland surface creation, first commit to `wl_surface.enter`, p99 | ≤ 50 ms |
| `libdecor` decoration attach, p99 | ≤ 100 ms |
| `xdg_positioner` popup creation, p99 | ≤ 20 ms |
| Nested grab session dispatches in 500 ms | ≥ 30 |
| `wl_surface.set_buffer_scale` applied before the next buffer attach | 100% |

**Workload:** identical in shape to the macOS slice, so the two platforms are comparable: 400 steady frames, a 60-step live resize, and a 500 ms nested grab session, each with a 4096 record scene handed across the stage boundary.

**Baseline to beat:** the macOS slice measured a present interval p50 of 16.67 ms with a p99 between 17.7 ms and 45.4 ms across five runs of the same binary, and a UI thread block p50 of 16.57 ms that A1.2a attributed to layer recycling rather than to GPU submission.

## Reference machine and compositor — must be filled before the run

§28.4 requires the workload, the hardware and the threshold to be recorded *before* the measurement. The rows below are deliberately empty: the run must not start until they name the exact host and compositor.

| Field | Value |
| --- | --- |
| CPU | _fill in before the run_ |
| GPU / driver | _fill in before the run_ |
| Kernel | _fill in before the run_ |
| Compositor and version | _fill in before the run_ |
| `xdg-shell` and `libdecor` versions | _fill in before the run_ |
| Output refresh and scale | _fill in before the run_ |
| Preset | `release` |

## Implementation slices

| Slice | Scope | Status |
| --- | --- | --- |
| **A2-L1** | `wl_display` connection, `wl_compositor`, `wl_surface`, `wl_frame` loop. Answers Q1. | scaffold only |
| **A2-L2** | `xdg_shell` toplevel, `libdecor` client side decoration. Answers Q2. | not started |
| **A2-L3** | `xdg_positioner` popup as the nested grab session. Answers Q3. | not started |
| **A2-L4** | `set_buffer_scale` and `set_buffer_transform` ahead of the frame. Answers Q4. | not started |
| **A2-L5** | Round trip cost attribution against the R1 budget. Answers Q5. | not started |

## Build wiring

`platform.linux.c++` and `window.linux.c++` are **scaffolds and are not build targets yet**: they declare the opaque state, the pacing contract and the decoration/popup contract but do not implement the entry points of `platform.h`, so adding them to `prototypes/a2/CMakeLists.txt` would create a target that cannot compile. They gain a target in A2-L1, behind `if(UNIX AND NOT APPLE)` with the Wayland and `libdecor` dependencies.

They are still linted: `check-format.sh` matches the `.c++` suffix, so `bash build/check-format.sh` checks them on every host, and each file refuses to compile anywhere but Linux through an `#error` guard.
