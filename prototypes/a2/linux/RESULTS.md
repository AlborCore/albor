# Prototype A2 — Linux slice results

## Status

`PENDING_CI_MATRIX_RUN (Requires Wayland Host)`

Nothing in this file is a measurement. The recording host is macOS, which has no Wayland compositor, no `xdg_shell`, no `libdecor` and no `xdg_positioner`, so none of the code in this slice has been compiled or executed. The thresholds in [`../linux/plan.md`](../linux/plan.md) were recorded before any run, per §28.4, and must not be edited to fit a result.

## Execution tables

| Slice | Question | Threshold | Measured | Status |
| --- | --- | --- | --- | --- |
| A2-L1 | `wl_frame` callback rate | ≤ 1 per refresh, 100% of the time | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L1 | Burst coalescing | ≥ 95% of burst requests become one frame | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L1 | Present interval | p99 ≤ 16.7 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L1 | UI thread blocked by the round trip | p99 ≤ 1.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L2 | Surface creation to `wl_surface.enter` | p99 ≤ 50 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L2 | `libdecor` decoration attach | p99 ≤ 100 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L2 | Live resize through `xdg_toplevel.configure` | ≥ 95% of the resize steps | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L3 | `xdg_positioner` popup creation | p99 ≤ 20 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L3 | Nested grab session dispatches | ≥ 30 in 500 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L4 | `set_buffer_scale` before the next attach | 100% | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L5 | Input to present | p99 ≤ 25.0 ms (R1) | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L5 | Scene build on the UI thread | p99 ≤ 2.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |
| A2-L5 | Recording and submitting a frame | p99 ≤ 8.33 ms | — | `PENDING_CI_MATRIX_RUN (Requires Wayland Host)` |

## What the macOS slice already answers for this one

`CAMetalLayer` returned about one drawable per display cycle and exposed no non-blocking availability check, so the UI thread blocked a median of 16.57 ms inside `nextDrawable` and the cadence doubled whenever a frame was requested late. `wl_frame` is the opposite design: the compositor hands the client a callback at most once per refresh and the client coalesces everything in between. That difference is the main reason this slice is worth running rather than extrapolating from macOS, and it is the one number that decides whether A2's back-pressure finding generalises.

## Reference machine and compositor

Empty until the run host and compositor are named. See the reference machine table in [`../linux/plan.md`](../linux/plan.md); §28.4 forbids starting the run before it is filled in.
