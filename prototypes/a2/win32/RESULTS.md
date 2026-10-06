# Prototype A2 — Windows slice results

## Status

`PENDING_CI_MATRIX_RUN (Requires Windows Host)`

Nothing in this file is a measurement. The recording host is macOS, so no Win32, DXGI, D3D12 or DirectComposition code has been compiled or executed. The thresholds in [`../win32/plan.md`](../win32/plan.md) were recorded before any run, per §28.4, and must not be edited to fit a result.

## Execution tables

| Slice | Question | Threshold | Measured | Status |
| --- | --- | --- | --- | --- |
| A2-W1 | DirectComposition + DXGI cadence | present interval p99 ≤ 16.7 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W2 | `WM_ENTERSIZEMOVE` nested loop | ≥ 30 dispatches in 500 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W2 | UI thread unblocked in the nested loop | blocked p99 ≤ 1.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W3 | `WM_DPICHANGED` before the next frame | 100% of the messages | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W4 | Frames in flight before coalescing | ≥ 2 | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W4 | UI thread unblocked by back pressure | blocked p99 ≤ 1.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W5 | `DisplayContent` scaling tail | present interval p99 ≤ 16.7 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W5 | Swap chain recreation | live resize frames ≥ 95% | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W1 | `SetContent` commit | p99 ≤ 1.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W1 | Input to present | p99 ≤ 25.0 ms (R1) | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W1 | Scene build on the UI thread | p99 ≤ 2.0 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |
| A2-W1 | Recording and submitting a frame | p99 ≤ 8.33 ms | — | `PENDING_CI_MATRIX_RUN (Requires Windows Host)` |

## What the macOS slice already answers for this one

The macOS slice is the only executed A2 measurement, and it constrains what the Windows run must show. `t_acquire` — waiting for a drawable from `CAMetalLayer` — cost 1.30 ms at p50 and 20.75 ms at p99, while recording and submitting cost 0.05 ms at p50 and 0.12 ms at p99. Back pressure came from the presentation surface recycling drawables, not from the gpu. A Windows run that shows a similar profile in `IDXGISwapChain::Present` is expected, and a run that does not is a finding worth recording.

## Reference machine

Empty until the run host is named. See the reference machine table in [`../win32/plan.md`](../win32/plan.md); §28.4 forbids starting the run before it is filled in.
