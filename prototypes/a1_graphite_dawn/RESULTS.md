# Prototype A1-Graphite-Dawn — Results

Plan committed in [`plan.md`](plan.md) before the spike existed (§28.4). Supersedes the first attempt's T5.1
failure, which is kept below as history because the reason it failed turned out to be a manifest omission rather
than a missing capability.

## Executive result

**WORKS — shared-device Graphite+Dawn is operational.**

A `skgpu::graphite::Context` was created on a `wgpu::Device` this process created, reported
`backend() == skgpu::BackendApi::kDawn`, recorded Skia drawing into an offscreen surface, executed on that device,
and read its pixels back through Graphite's own async read. That is the architecture AD-28 was blocked on, and it
is the answer to the task's central question: **Graphite can run on the same Dawn device Albor's renderer uses.**

This is a feasibility result, not a shipping recommendation, and two things it does *not* establish are recorded
below: the text workload could not be measured, and the W1/W2 timings did not complete in the time available.

## Environment

| | |
| --- | --- |
| machine | Intel Core i7-9750H @ 2.60 GHz, 6c/12t, 16 GB |
| OS | macOS 26.6.2 (build 25F74) |
| compiler | Apple Clang 17 (vcpkg `x64-osx-dbg`, no ccache in the path) |
| Skia | milestone 148, vcpkg port, `skia[core,dng,gl,graphite,harfbuzz,icu,jpeg,metal,png,vulkan,webp,dawn]` on osx |
| Dawn | vcpkg `dawn[metal]`, the same package A1-Dawn links, so one Dawn in the process |
| vcpkg | revision `bf04c909169fdbb30821c02c6eb01f1cd1295d05` |
| Skia REF | `e7c90ecca9444fe09598f1630ab7cee2c0ee027a` |
| GPU backend | `metal/integrated "Intel(R) UHD Graphics 630"` |

**The adapter is an integrated GPU and the earlier slices' hardware line is wrong.** `graphite_dawn_main.cpp`
printed "i7-9750H macOS with the AMD Radeon Pro 5300M" from a hardcoded string, and plan.md records no GPU at all.
Dawn reports Intel UHD 630. Every timing here is therefore an *integrated*-GPU figure and is **not** comparable to
any earlier slice's numbers without saying so. This needs correcting in the spike's own output.

## The build problem, resolved — and it was not a missing feature

The first attempt recorded T5.1 as a link failure: `ContextFactory::MakeDawn` undefined. The committed analysis was
right that the fix was a manifest feature, and the blocker was never a missing capability:

- vcpkg's skia port ships a **`dawn` feature** (alongside `avif`, `direct3d`, `gl`, `graphite`, `vulkan`, …). It was
  simply never requested.
- That feature sets `skia_use_dawn=true`, and `gn/skia.gni:189` guards it with
  `assert(!skia_use_dawn || skia_enable_graphite)  # Dawn is Graphite-only`, so it *is* the Graphite Dawn backend.
- `BUILD.gn:1183` honours it with `sources += skia_graphite_dawn_sources`.

The change is one line in `vcpkg.json`. **No dependency version moved**, and `metal` was kept so the Ganesh and
Graphite-on-Metal baselines still link.

**Header presence was not treated as evidence.** The library was checked directly:

| | before | after |
| --- | --- | --- |
| `ContextFactory::MakeDawn` | 0 symbols | defined (`T`) |
| `BackendTextures::MakeDawn`, `TextureInfos::MakeDawn`, `DawnTexture::MakeDawnTexture` | absent | defined |
| `MakeMetal` / `MakeVulkan` | defined | defined, unchanged |

## Architecture actually measured

```text
        this process
              |
    wgpu::CreateInstance   (requests InstanceFeatureName::TimedWaitAny)
              |
    RequestAdapter -> CreateDevice -> GetQueue          one instance, one adapter,
              |                                          one device, one queue
              +-----------------------------------------------+
              |                    |                          |
    skgpu::graphite::DawnBackendContext{instance, device, queue}
              |
    ContextFactory::MakeDawn  ->  backend() == kDawn
              |
    makeRecorder -> SkSurfaces::RenderTarget -> record -> submit -> fence -> asyncReadPixels
```

**Configuration A (shared device) is what runs.** `DawnBackendContext` takes the client's `Instance`, `Device` and
`Queue` by value, and it is a plain struct with public fields.

**Configuration B is not representable, not merely untested.** There is no constructor, factory or code path in which
Graphite creates a Dawn device; the only way to obtain a Dawn-backed Graphite context is to hand it one. So the
"two independent devices" fallback cannot be built against this API, and the fallback question does not arise.

Identity diagnostics recorded: one `CreateInstance`, one `RequestAdapter`, one `CreateDevice`, one `GetQueue` in the
process; `context->backend()` is `kDawn`. **Not proven:** byte-level identity of the underlying Dawn device handle.
`Context` exposes no accessor for its backend context and `wgpu::Instance` in this Dawn has no feature or handle
query, so the argument rests on the API's structure (Graphite cannot own a device) plus the backend tag, not on a
pointer comparison.

## Integration requirements discovered

1. **The instance must request `InstanceFeatureName::TimedWaitAny`.** Graphite's Dawn queue manager implements
   `SyncToCpu` as a Dawn *timeout* wait (`DawnQueueManager.cpp:95`). With a default instance the first fenced submit
   aborts inside Skia: `check(status == wgpu::WaitStatus::Success)` fails with *"Timeout waits are either not
   enabled or not supported."* This is a mandatory client obligation of the shared-device architecture and it is not
   documented in the header.
2. **`Context::checkForFinishedWork` is private in milestone 148.** The API for waiting on outstanding work without
   submitting anything is not callable, so separating the enqueue cost from the GPU fence requires a *second*
   `submit()`, which makes Skia log `Submit called with no active command buffer!` once per frame. The separation is
   still worth having; the log is noise.
3. **`TextureInfo`'s constructor from backend data is private**, and so is `BackendTexture`'s. A client therefore
   **cannot hand Graphite a texture it allocated**, even though `DawnTextureInfo(WGPUTexture)` is public and
   `TextureInfos::MakeDawn` / `BackendTextures::MakeDawn` exist as exported symbols. This generalises A1.4's Metal
   finding from a Metal-specific gap to a Skia API gap: Graphite allocates its own surfaces, and a client-owned
   render target — the natural way to present one Graphite frame inside a Dawn frame — needs an upstream patch.
   **This is the most consequential finding for the "one presentation path" goal.**

## Smoke test

| stage | result |
| --- | --- |
| Dawn instance, adapter, device, queue | pass — `metal/integrated "Intel(R) UHD Graphics 630"` |
| Graphite context on that device | **pass** — `T5.1`, `backend() == kDawn` |
| surface creation | pass |
| one primitive recorded | pass |
| fenced submit / GPU drain | pass — `T5.6`, `hasUnfinishedGpuWork()` false |
| readback and pixel verification | **inconclusive — see below** |
| Graphite text | **not verifiable — see below** |

From the pre-registered spike (`albor-prototype-a1-graphite-dawn`), unchanged thresholds:

| id | threshold | result |
| --- | --- | --- |
| T5.1 | graphite context on an externally created dawn device | **PASS** (was FAIL) |
| T5.2 | adapter reports a hardware backend | PASS — `metal/integrated` |
| T5.3 | geometry and Skia text in one frame on one device | PASS as recorded, **but see the text caveat** |
| T5.4 | combined record p50 ≤ 2 × geometry-only | **FAIL 6.05×** (0.073 → 0.439 ms) — **not a valid result** |
| T5.5 | ≥ 55 fps of recorded frames | PASS — 2215.6 fps, offscreen and unpresented |
| T5.6 | GPU work observed and drained | PASS |

**T5.4's failure is not evidence about text.** A1's resource factory resolves its typeface through
`SkFontMgr_New_CoreText(nullptr)->matchFamilyStyle(nullptr, {})`, which returns null in this process, so the spike's
"text" pass recorded no glyphs. The 0.419 ms attributed to text is the cost of 24 `drawSimpleText` calls that drew
nothing, and the 6.05× ratio is that figure over a 4 000-segment geometry pass. The threshold is pre-registered and
has not been changed; it has simply not been validly measured, and it must be re-run with a real face.

## The text blocker

**This Skia build has exactly one font manager: `SkFontMgr_New_CoreText`.** `nm` finds no FreeType symbols and no
custom manager, and `SkTypeface` in milestone 148 has only `MakeEmpty()` and `MakeDeserialize()` — there is no
`MakeFromData`, so a face cannot be created from a font file either.

So if CoreText's collection is unavailable to this process, **no text is drawable and the entire reason to keep Skia
is untestable in this configuration.** The driver reports every route it tried (`F1`) rather than assuming one.
This must be resolved before any W2 figure, any text claim, or any AD-28 conclusion about Skia's 2D surface is
worth anything.

## Workload measurements — NOT obtained

**No W1, W2 or W3 timing is reported here.** The driver
(`graphite_dawn_bench.cpp`) implements them — 1.8 × 10⁶ segments on A1's `0xA11F5EED` seed, 100 000 symbols with
**4 000 labels** on `0xCAD0FFEE`, A1's label paint and draw loop, with recording, enqueue, GPU fence and readback on
separate clocks — but a full run did not complete in the time available and was stopped rather than reported
partially.

Why it is slow, measured rather than guessed: each frame fences on the GPU, every frame rasterises 1.8 × 10⁶ filled
quads, and the adapter is an **integrated** GPU. A1.4's Graphite-on-Metal recording figure (1.521 ms p99) is
CPU-side only, so it is no guide to the fenced per-frame cost. The obvious lever is fewer frames per repetition, but
that changes the timing policy the other slices used, so it is a decision rather than a tweak.

| metric | Ganesh (A1.3) | Graphite/Metal (A1.4) | Dawn-only (A1-Dawn) | Graphite+Dawn |
| --- | ---: | ---: | ---: | ---: |
| W1 recording | 0.394 ms p99 | 1.521 ms p99 | **0.085 ms p50** | not measured |
| W1 draw calls | 220 | not recorded | **1 `Draw(6, 1 800 000)`** | not observable |
| W1 GPU execution | N/A | N/A (A1.4 had no fence) | N/A | not measured |
| W1 present interval | N/A | N/A | 16.62 ms p50 | not measured |
| W2 recording (geometry) | recorded | not run | recorded | not measured |
| W2 text recording | recorded (4 000 labels) | not run | **never drew glyphs** | not measured |
| W3 | two windows + 100 ms hold | not run | not run | **not reproducible offscreen** |
| pixel parity | — | — | T2 deterministic, batch parity 13.95%/63.68% unfixed | not measured |
| 2D feature matrix | — | — | N/A | **not run** |

Draw-call counts are reported as *not observable* rather than guessed: Graphite issues its own Dawn passes and
WebGPU exposes no counter, so counting them needs a Dawn interception build or Skia instrumentation.

## Pixel validation, features, headless, cross-platform — NOT done

- **Pixel validation**: the driver has the machinery (repeat capture, exact per-channel diff, ink bounding box) and
  asserts that a drawn 80 × 40 rect lights exactly 3 200 pixels in exactly the requested box. It has not completed a
  run, so **no repeatability or parity figure is claimed.**
- **2D feature matrix**: not run. This is the largest remaining gap, since paths, joins, caps, dashes, clipping,
  transforms, opacity and gradients are the capabilities that motivated keeping Skia.
- **Software/headless (AD-26)**: unresolved. Dawn's CPU adapter is the only software path visible in this build, and
  Graphite's Dawn backend on it is untested. Nothing here shows a software rasterizer exists for Graphite+Dawn, and
  AD-26 must not be read as satisfied.
- **Cross-platform**: nothing verified. Dawn advertises D3D12 and Vulkan and Skia's Dawn backend is backend-agnostic
  in principle, but **no claim is made** — only macOS/Metal was run. Note that the vcpkg skia port's `dawn`
  feature is `"!android & !uwp"` with `x11` pulled in on Linux, which is the manifest-level starting point, not
  evidence.

## What this says about AD-28 — and what it does not

**Blocker (6), "Skia Graphite on a Dawn backend untested", is now answered: it works.** That is a real change to
AD-28's evidence, and it is the first *positive* finding the renderer investigation has produced.

It does **not** make Graphite+Dawn the primary renderer, for reasons that are independent of the missing benchmarks:

1. **One presentation path is not achievable today.** Graphite cannot render into a client-allocated Dawn texture
   through the public API. Albor's custom geometry and its presentation both live in Dawn, so Graphite's output
   cannot join a Dawn frame without an upstream Skia patch. "One device, one queue, one frame" is true for
   *submission* and false for *composition*.
2. **The advantage that motivated it is unmeasured.** Dawn's `Draw(6, 1 800 000)` is the reason to want Dawn; whether
   Graphite preserves it on the same device is exactly the W1 number this prototype did not finish.
3. **Skia's text advantage is currently untestable here**, so the other half of the trade is unproven too.
4. **Dawn-only is not slower on the recorded evidence** — 0.085 ms p50 recording against Ganesh's 0.394 ms p99, and
   the only GPU-paced figure A1-Dawn has (16.62 ms present) is a presentation number that §14 says must not be used
   as renderer evidence on this machine.

**Recommendation: keep as experimental, and do not freeze AD-28.** Configuration A is real and the port change was
one line, so the option stays open cheaply. But the two questions that would justify it — does Graphite keep Dawn's
batched-geometry advantage, and can its output be composited into a Dawn frame — are both still open, and the second
needs an upstream Skia change rather than a measurement.

## The minimum next step

Three things, in order, and none of them is another benchmark:

1. **Fix the typeface**, or record that text is unmeasurable in this build's configuration. Everything about Skia's
   2D surface is blocked on it.
2. **Reduce the frame count for the heavy workloads** and re-run W1, accepting a documented deviation from the other
   slices' 5 × 20 policy so that a number exists at all.
3. **Decide the composition question**, because it is architectural rather than measurable: either accept an
   upstream Skia patch to export a client-owned `BackendTexture` for Dawn, or accept that Graphite output reaches the
   screen through its own surface and not through Albor's Dawn frame.

## Problems encountered

| problem | resolution |
| --- | --- |
| `ContextFactory::MakeDawn` undefined | the port's `dawn` feature was never requested; added, rebuilt, symbol verified |
| first fenced submit aborts in `DawnQueueManager.cpp:95` | instance must request `InstanceFeatureName::TimedWaitAny` |
| `Context::checkForFinishedWork` is private | fence with a second `submit()`; Skia logs per frame |
| factory typeface is null | only `SkFontMgr_New_CoreText` exists in this build; unresolved, blocks all text |
| spike prints a GPU it never queried | prints Dawn's adapter instead; the hardcoded Radeon line is wrong |
| `SkImage::RescaleGamma` has no `kNo` | it is `kSrc`/`kLinear` |
| full W1/W2 run did not finish | stopped rather than reported partially; 1.8 × 10⁶ fenced quads/frame on an integrated GPU |
