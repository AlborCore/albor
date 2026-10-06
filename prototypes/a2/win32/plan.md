# Prototype A2 — Windows slice (Win32 / DirectComposition / DXGI)

Pre-recorded plan for the Windows platform slice of Prototype A2 (§30, AD-16, AD-18 R1). This slice is **not yet executed**: the recording host is a macOS machine, so the implementation files here are scaffolds and the measurement tables stay `PENDING_CI_MATRIX_RUN`. Nothing below may be edited after a run; a change of threshold requires a new revision and a new run, exactly like A2 Revision R1.

Prototype code: not linked by any runtime module (AGENTS.md).

## Question

| # | Question | Spec |
| --- | --- | --- |
| Q1 | Does `WM_ENTERSIZEMOVE` let the UI thread keep servicing input, layout and presentation while the modal move/size loop is open? | §7.4, §8 |
| Q2 | Does `WM_DPICHANGED` update the authoritative metrics before the next frame is built? | §9.1, §7.4 |
| Q3 | Does DirectComposition with DXGI swap chains reach the frame cadence that `CAMetalLayer` reached on macOS? | §9.2, §10.3, AD-16 |
| Q4 | How many frames may be in flight before the swap chain applies back pressure, and does coalescing keep the UI thread unblocked? | §10.3, AD-18 R1 |
| Q5 | Does `DisplayContent` scaling and swap-chain recreation show up as a frame tail the macOS slice never saw? | §9.2 |

## Thresholds, recorded before the run

| Category | Threshold |
| --- | --- |
| Present interval p99 | ≤ 16.7 ms at a 60 Hz reference display |
| UI thread blocked by presentation or by swap-chain back pressure, p99 | ≤ 1.0 ms |
| Scene build on the UI thread, p99 | ≤ 2.0 ms |
| Recording and submitting a frame on the render thread, p99 | ≤ 8.33 ms (the R1 processing deadline) |
| Input-to-present p99 | ≤ 25.0 ms (Revision R1 of the A2 budget) |
| Frames presented while a live resize is in flight | ≥ 95% of the resize steps |
| `WM_ENTERSIZEMOVE` nested loop: input/layout/presentation dispatches in 500 ms | ≥ 30 |
| `WM_DPICHANGED` handled before the next frame is built | 100% of the messages |
| Frames in flight allowed before coalescing | ≥ 2 (AD-18 R1 budget) |
| DirectComposition `SetContent` commit, p99 | ≤ 1.0 ms |

**Workload:** identical in shape to the macOS slice, so the two platforms are comparable: 400 steady frames, a 60-step live resize, and a 500 ms nested modal session, each with a 4096 record scene handed across the stage boundary.

**Baseline to beat:** the macOS slice measured a present interval p50 of 16.67 ms with a p99 between 17.7 ms and 45.4 ms across five runs of the same binary, and a UI thread block p50 of 16.57 ms that A1.2a attributed to layer recycling rather than to GPU submission.

## Reference machine — must be filled before the run

§28.4 requires the workload, the hardware and the threshold to be recorded *before* the measurement. The row below is deliberately empty: the run must not start until it names the exact host.

| Field | Value |
| --- | --- |
| CPU | _fill in before the run_ |
| GPU / driver | _fill in before the run_ |
| OS build | _fill in before the run_ |
| Display refresh and scale | _fill in before the run_ |
| Preset | `release` |

## Implementation slices

| Slice | Scope | Status |
| --- | --- | --- |
| **A2-W1** | Win32 window, `DXGI` swap chain, DirectComposition visual tree. Answers Q3. | scaffold only |
| **A2-W2** | `WM_ENTERSIZEMOVE` nested loop with the frame source attached to a common-mode-equivalent pump. Answers Q1. | not started |
| **A2-W3** | `WM_DPICHANGED` metrics update ahead of the frame build. Answers Q2. | not started |
| **A2-W4** | Frames-in-flight budget and coalescing. Answers Q4. | not started |
| **A2-W5** | `DisplayContent` scaling and swap-chain recreation. Answers Q5. | not started |

## Build wiring

`platform.win32.c++` and `window.win32.c++` are **scaffolds and are not build targets yet**: they declare the opaque state and the message contract but do not implement the entry points of `platform.h`, so adding them to `prototypes/a2/CMakeLists.txt` would create a target that cannot compile. They gain a target in A2-W1, behind `if(WIN32)`.

They are still linted: `check-format.sh` matches the `.c++` suffix, so `bash build/check-format.sh` checks them on every host, and each file refuses to compile anywhere but Windows through an `#error` guard.
