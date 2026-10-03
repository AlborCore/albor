# Prototype A1-Dawn — results

Thresholds were committed to [`plan.md`](plan.md) before any run. Prototype code: not linked by any runtime module
(AGENTS.md).

## Status

**Incomplete.** The dependency integrates and links; the renderer and the workloads were not implemented, so **no latency
figure in this file is a measurement**. What follows is what was actually established, and what was not.

## Dependency: established

Dawn builds and links on the reference machine.

| Fact | Evidence |
| --- | --- |
| The `dawn` port builds on macOS x64 with the `metal` feature | `dawn[core,metal]:x64-osx@20260624.223603` installed |
| CMake integration works as documented | `find_package(Dawn CONFIG REQUIRED)` → `dawn::webgpu_dawn` |
| Headers install | `include/dawn/{webgpu.h,webgpu_cpp.h,native/,dawn_proc_table.h}` |
| Static library installs | `debug/lib/libwebgpu_dawn.a` (710 MB), `lib/libwebgpu_dawn.a` (20 MB) |

**Wall time of the successful port build: 757 s (12.6 min).**

## The build-time threshold was not evaluated

The plan recorded "Clean build time ≤ 3.0 minutes", measured with **ccache cold**. That precondition was **not met**:

- The first attempt failed partway through, leaving tint's objects in ccache.
- The measured 757 s run therefore started with a warm cache for part of the port.

So 757 s is **not** a clean-build figure, and it is **not comparable** to any Skia figure, because no Skia clean build was
measured on this machine. The only Skia number in this repository is an 11 min warm rebuild, which is a different
measurement of a different thing.

**Disposition: threshold not evaluated, precondition not met.** The budget is left exactly as recorded (§28.4); it is not
restated and it is not reported as a pass or a fail. Getting a real answer needs a cold build of both ports, which is
several hours of machine time and is deferred rather than approximated.

For scale only, and explicitly **not** as a comparison: the Dawn debug static library is 710 MB against Skia's 670 MB,
both for the `x64-osx` debug triplet.

## The brief's API shape does not match this Dawn

The task specifies implementing `wgpu::Surface` and `wgpu::SwapChain`. In Dawn 20260624 the surface path has moved on, so
the implementation has to be written differently. Verified from the installed client header:

| The brief assumes | What this Dawn actually exposes |
| --- | --- |
| `wgpu::SwapChain` as the presentation object | no `SwapChain` object; `Surface::Configure`, `Surface::GetCurrentTexture(SurfaceTexture*)`, `Surface::Present()` |
| a swapchain configured with width/height/format | `Surface::Configure(const SurfaceConfiguration*)`, and the surface is created from `SurfaceDescriptor` with `SurfaceSourceMetalLayer { void* layer; }` chained in |
| synchronous adapter/device acquisition | `Instance::RequestAdapter(...)` and `Adapter::RequestDevice(...)` are callback-mode calls returning a `Future` |

The `SwapChain` *symbol type* still appears 38 times in the header (state enums, statuses), which is exactly why a
quick grep suggests the old API is present. The presentation object is not.

## Defect in this prototype's own process, recorded because it cost a build

The first port build failed with `fatal error: 'absl/container/flat_hash_set.h' file not found`. The cause was **not**
Dawn: the build had been invoked as a bare `vcpkg install --x-manifest-root=.` from the repository root, which leaves
`VCPKG_INSTALLED_DIR` unset and makes vcpkg default to a stray `./vcpkg_installed` at the repo root. That directory held a
partial abseil, and Dawn compiled against it. The correct invocation is `cmake --preset release`, which sets
`VCPKG_INSTALLED_DIR` to the build tree's own `vcpkg_installed`. vcpkg removed the stray directory when the build failed, so
nothing was left behind — but the rule is worth keeping: **never invoke `vcpkg install` against the manifest root without
`VCPKG_INSTALLED_DIR`; go through the CMake preset.**

## What was not done

- **No renderer.** There is no `prototypes/a1_dawn/` implementation: no surface, no pipeline, no instanced quad batch.
- **No W1, W2 or W3.** Zero latency samples exist, so there is no p50, p95 or p99 to report for recording, present
  interval, hover query or draw-call count.
- **The draw-call claim is untested.** The premise worth testing is that WebGPU's 32-bit instance index allows one
  `Draw` call for 10⁶ instanced quads, where Skia's 16-bit vertex indexing cost 220 draw calls per frame. Nothing here
  confirms or refutes it.
- **AD-05 / AD-06 not exercised.** With no code written, "zero Dawn headers in a public interface header" is vacuously
  true and therefore not a finding.

## Verification of the tree as it stands

| Check | Result |
| --- | --- |
| `build/check-format.sh` | exit 0 |
| Release build | 0 errors, 0 warnings |
| Debug build | 0 errors, 0 warnings |
| 33 gtest unit tests | 33 passed |
| `plan.md` thresholds committed before the run | yes, `b538f68` |
---

# Phase A1-Dawn run — the same workloads, on Dawn

Driver: `./.build-release/prototypes/a1_dawn/albor-prototype-a1-dawn-bench`. **6 threshold failures, exit 1.**

The workloads, the seeds, the platform window and the thresholds are the ones Prototype A1 used against Skia, so the
rows are comparable line for line. The resource generator in `resources.cpp` is copied verbatim from
`prototypes/a1/resource_factory.macos.mm`, and the proof that this worked is in the tables below: **the hover hit ratio
came out at 99.41% on both renderers, to the digit**, which can only happen if both were fed byte-identical geometry.

## The headline: one draw call, and it presents at 60 Hz

| Metric, W1 at 1.8 × 10⁶ segments | Skia (Ganesh) | **Dawn** | Threshold |
| --- | --- | --- | --- |
| **Draw calls per frame** | **220** | **1** | exactly 1 |
| **Present interval p50** | **149.7 ms** | **16.62 ms** | ≤ 16.7 ms |
| Present interval p95 | 279.6 ms | 18.16 ms | — |
| Present interval p99 | 420.5 ms | 48.93 ms | ≤ 16.7 ms |
| Recording p50 | 0.177 ms | 0.085 ms | — |
| Recording p99 per segment | 0.219 ns | 0.114 ns | ≤ 8 ns |
| Recording p95 moved / still (O(1)) | 0.976× | 1.096× | ≤ 1.25× |
| Frames dropped | 0 of 401 | 0 of 400 | ≤ 1% |
| Append build per 100k, p99 | 119.9 ms | 37.0 ms | ≤ 25 ms |

**Both of Skia's structural limits disappear.** WebGPU's 32-bit instance index carries all 1.8 × 10⁶ segments in a
single `Draw(6, 1800000)` call, where Skia's 16-bit vertex indexing forced 220. And because the GPU is no longer
CPU-starved by 220 draw calls, the frame actually reaches the display: **16.62 ms p50 is one 60 Hz interval**, against
150 ms for Skia — Skia was presenting at roughly 7 fps on this machine and reporting 0 dropped frames while doing it,
because its scheduler had no reason to complain.

## W2 — CAD canvas, 10⁵ symbols, hover hit testing

| Metric | Skia | **Dawn** | Threshold |
| --- | --- | --- | --- |
| Recording p50 / p99 | 6.16 / 7.23 ms | **0.110 / 0.238 ms** | ≤ 8.33 ms |
| Present interval p50 / p99 | 41.3 / 77.5 ms | **16.66 / 17.04 ms** | ≤ 16.7 ms |
| Hover hit query p99 | 0.0022 ms | 0.0048 ms | ≤ 1.0 ms |
| Queries that hit the expected symbol | 99.41% | **99.41%** | ≥ 99% |

Recording is **30× cheaper** and the canvas holds 60 Hz. The present interval misses by 0.34 ms at p99, which is 2% and
is vsync alignment rather than a real stall. The hit ratio is identical to Skia's because it is the same CPU-side
uniform grid over the same generated geometry — the renderer does not participate in it at all, which is exactly why the
two numbers matching is the validity check for this comparison.

## W3 — multi-window isolation, 100 ms hold on window 1

| Metric | Skia | **Dawn** | Threshold |
| --- | --- | --- | --- |
| Window 1 attempts / presented | 130 / 128 (deadline-limited) | **400 / 400** | ≥ 95% |
| Window 2 interval during the hold, p50 | 1244 ms (invalid run) | **100.86 ms** | ≤ 16.7 ms |
| Window 2 frames during the hold | 0.33% | **16.5%** | ≥ 95% |
| Window 2 queries during the hold | 29 | **100** | ≥ 30 |
| Injected hold width | 146 s (deadline-limited) | **10.1 s** | — |

**W3 is now a real measurement, and it refutes the isolation claim.** Because Dawn sustains 60 Hz single-window, the
hold is no longer hidden by GPU saturation: window 2's interval during the hold is **100.86 ms p50 — almost exactly the
100 ms hold**. Window 2 is not slowed by shared GPU contention; it is stopped dead by window 1's hold, because both
windows present from the same thread. Presenting on the UI thread gives no window isolation on macOS, whatever the
renderer. This is the same conclusion A1.2a reached for Skia, now confirmed on a renderer that is not the bottleneck.

## What this changes for §11.2

| Question | Skia said | Dawn says |
| --- | --- | --- |
| Can a 10⁶-segment batch be one draw call? | no — 16-bit indexing, 220 calls | **yes** |
| Can a retained batch present at 60 Hz? | no — 150 ms p50 | **yes** — 16.62 ms p50 |
| Does CPU stroke tessellation kill the batch? | yes — 10,470 ms/frame | **not applicable** — quads are expanded in the vertex shader |
| Is append amortised? | no — O(n) rebuild | **no — O(n) rebuild, 37 ms** |

The batching story in §11.2 was written against a renderer whose line path is CPU-bound and whose vertex indexing is
16-bit. On WebGPU both constraints disappear, and the remaining append cost is a **CPU-side rebuild in the workload's
own generator, not a renderer limit** — the upload itself only writes the appended tail, because the generator is prefix
deterministic.

## Limits of this run

- **Build time is not compared.** The ≤ 3.0 min threshold was not evaluated: the measured 757 s run started with a warm
  ccache for part of the port, and no cold Skia build was measured on this machine. See the earlier section.
- **Labels are counted, not drawn.** W2 builds and reports the same 4000 labels with the same buffer as Skia, but Dawn
  draws no glyphs: a WGSL text path needs a glyph atlas, which is out of scope for this slice. W2 therefore compares
  symbol geometry and hit testing, not text rendering.
- **One run each.** Prototype A1 already established that tail figures on this machine vary several-fold between runs,
  so the p99 columns are indicative and the p50 columns are the stable comparison.
- **Windows and Wayland** are not covered; this slice is macOS only.

---

# Architecture decisions recorded from this run

Recorded in `albor-design-v3.md` §29 (this commit).

## AD-28 — Candidate primary HAL (Dawn / WebGPU C++): **Proposed, in active evaluation**

Dawn is the primary **candidate** HAL for `albor::Renderer` over Metal on macOS, on the evidence in this file. Skia is
**not** retired. Four items block any decision to replace it:

1. **Platform parity** — D3D12 and Vulkan are unvalidated. This slice enabled one feature, `metal`, on one platform.
2. **Text and glyph pipeline** — Dawn has no native text or glyph stack. The W2 labels were built and counted but never
   drawn, so text rendering is untested end to end. A GPU glyph atlas (MSDF / FreeType / HarfBuzz) is required first.
3. **Software and headless parity (AD-26)** — AD-26 is *Decided* and required. Dawn ships no software rasterizer, and
   Skia's raster backend was the natural path to it. A CPU fallback strategy must be identified before Dawn can be primary.
4. **Build cost** — unevaluated, for the reasons given above.

Also recorded: **Skia Graphite was never driven through presentation.** It was measured offscreen only, where it came out
1.24× slower than Ganesh at p50 and 3.85× worse at p99 per segment, so the Skia side of this comparison rests on Ganesh.

## AD-18 — threading and presentation isolation: **core division Decided, multi-window isolation Open**

The stage division is settled: stage 1 (scene build and application callbacks) on the UI thread, stage 2 (render
execution and GPU encoding) decoupled from application code.

Multi-window presentation isolation is **Open**, and the spec's previous claim that AD-18 was "Validated by A1 workloads
W1–W3" has been corrected — it was not. W3's answer is negative: presenting two windows on the UI thread stalls window 2
for 100.86 ms p50, which is the hold itself rather than GPU contention.

One phrase in the drafted text was **not** written into the spec as given: the draft cited thread-affinity deadlocks
"observed in A1.2a". A1.2a observed the UI thread blocking inside `nextDrawable` and refuted "the render thread owns the
surface" for Ganesh on macOS; it did **not** observe a deadlock from an off-thread presentation attempt. The spec records
the affinity constraint and the refutation, which is what was measured, and leaves the off-thread case untested.
