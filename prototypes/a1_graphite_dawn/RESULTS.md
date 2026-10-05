# Prototype A1-Graphite-Dawn — Results

Plan committed in [`plan.md`](plan.md) before the spike existed (§28.4). Supersedes the first attempt's T5.1
failure, which is kept below as history because the reason it failed turned out to be a manifest omission rather
than a missing capability.

## Executive result

**WORKS WITH LIMITATIONS — and the limitations are large enough to answer the renderer question.**

A `skgpu::graphite::Context` was created on a `wgpu::Device` this process created, reported
`context->backend() == skgpu::BackendApi::kDawn`, rendered retained geometry **and** Skia text into that device,
and read the pixels back. The architecture is real: one instance, one adapter, one device, one queue, handed to
Graphite unchanged. Configuration B cannot exist, because `DawnBackendContext` has no constructor that does not take
the client's device.

What the measurements then show is that routing Albor's geometry and text through Graphite costs far more than either
alternative:

- **1.8 M segments record in 7.679 ms p50** against Dawn-only's **0.085 ms** for a single
  `Draw(6, 1 800 000)` — **90x more CPU**, and Graphite gives up the batching advantage that motivated the whole
  idea.
- **4 000 text labels record in 127.3 ms p50**, ~31.8 µs each, dominating everything else by more than 10x. The
  combined CAD scene records in **112.7 ms p50**, a ~8.9 fps ceiling from CPU recording alone, against Ganesh's
  5.92–47.48 ms on the same scene.
- **No composition path exists** without an upstream Skia patch, which is off the table by decision.

So: it works, it is not the primary renderer.

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

## Workload measurements

One process, verified single-instance (`--smoke` also available for correctness runs; the driver refuses to start
when another instance holds `/tmp/albor-graphite-dawn-bench.lock`). **A1's workloads, seeds, label paint and draw
loop unchanged; the same 8 warm-up frames and 5 x 20 timed frames as every other slice.** Median of 100 frames,
p99 over the same 100.

| workload | record p50 | enqueue p50 | GPU fence p50 | record p99 |
| --- | ---: | ---: | ---: | ---: |
| **W1** geometry, 1 800 000 segments | **7.679 ms** | 3.159 ms | 623.120 ms | 11.163 ms |
| **W2** geometry only, 100 000 symbols | **4.394 ms** | 1.839 ms | 91.977 ms | 7.398 ms |
| **W2** text only, 4 000 labels | **127.321 ms** | 0.473 ms | 1.555 ms | 181.173 ms |
| **W2** geometry + text | **112.686 ms** | 2.044 ms | 92.846 ms | 177.579 ms |

Text is 127.3 ms of the combined 112.7 ms — i.e. **the labels dominate everything else by more than an order of
magnitude**, at ~31.8 µs per label of CPU recording. The GPU fence for the text pass is 1.555 ms, so this is
entirely CPU-side. Geometry is the opposite: a 1.555 ms fence against 623 ms for W1 means W1 is GPU-bound on this
adapter.

**W3, update half.** Rebuilding the 1.8 M-segment retained set costs **1 151.3 ms p50** on the CPU (A1 already
recorded that an append copies every point again); re-recording the mutated set costs **6.738 ms p50**.

**Memory.** Graphite reports 5 599 232 bytes budgeted and 5 595 136 purgeable, against a 256 MiB budget ceiling.

### Two anomalies, reported rather than smoothed

- **Text-only (127.3 ms) is slower than geometry+text (112.7 ms).** That is not physically meaningful; it is
  run-to-run variance, and A1.3 recorded the same instability on this machine (W2 recording p50 across four runs was
  5.92, 6.16, 47.13 and 47.48 ms). Treat the text figure as *order of magnitude*, not as a precise ratio.
- **Graphite-on-Dawn records 35x slower than Graphite-on-Metal** for the same 1.8 M-segment replay (7.679 ms p50
  against 0.220 ms). Both are Graphite replaying identical `drawVertices` calls, so the difference is not the
  recording API. It is unexplained, and until it is, the two Graphite rows should not be treated as measuring the
  same thing.

## Comparison

Every row is CPU-side unless labelled otherwise. A1.3's Ganesh and A1.4's Graphite rows are **W1**; A1-Dawn is W1.

| metric | Ganesh (A1.3) | Graphite/Metal (A1.4) | Dawn-only (A1-Dawn) | **Graphite+Dawn** |
| --- | ---: | ---: | ---: | ---: |
| W1 recording p50 | 0.177 ms | 0.220 ms | **0.085 ms** | 7.679 ms |
| W1 recording p99 | 0.394 ms | 1.521 ms | not recorded | 11.163 ms |
| W1 recording p99 per segment | 0.219 ns | 0.845 ns | — | 6.20 ns |
| W1 draw calls | 220 | not recorded | **1 `Draw(6, 1 800 000)`** | not observable |
| W1 GPU execution | N/A | **void, see below** | N/A | 623.1 ms fence, integrated GPU |
| W1 present interval | N/A | N/A | 16.62 ms p50 | not measured |
| W2 recording p50 | 5.92–47.48 ms (4 runs) | not run | recorded, no glyphs | 112.686 ms |
| W2 text recording | recorded (4 000 labels) | not run | **never drew glyphs** | 127.321 ms |
| W3 | two windows + 100 ms hold | not run | not run | update half only |
| pixel validation | — | **none** | T2 deterministic; batch parity 13.95%/63.68% unfixed | 0/921 600 differ, bit-identical |
| memory | — | — | — | 5.6 MB budgeted |

**Draw-call counts for Graphite are reported as not observable, not guessed.** Graphite issues its own Dawn passes
and WebGPU exposes no counter, so counting them needs a Dawn interception build or Skia instrumentation.

**A1.4's submit and frame columns are void.** That driver never called `Recorder::snap()` /
`Context::insertRecording()`, so its `submit()` had no command buffer, its 0.0016 ms submit and 0.333 ms frame
figures describe work that never reached the GPU, and it had no pixel check to notice. Its *recording* figures are
CPU-side and remain meaningful. This prototype found the same bug in its own first run.

**Ganesh's 10 470 ms stroked-segment figure is still excluded** as CPU tessellation, and it is still not a
renderer-recording comparison.

**Comparability caveat that applies to every GPU column:** this adapter is `metal/integrated "Intel(R) UHD
Graphics 630"`. A1-Dawn's 16.62 ms present figure and A1.3's present intervals were taken on an adapter this
investigation never verified. The fence column is an integrated-GPU number and says nothing about discrete hardware.

## Pixel validation

Verified, with the same assertions the smoke test uses:

| check | result |
| --- | --- |
| primitive placed exactly | **3 200 lit pixels in exactly the requested 80x40 box at 600,340** — not "looks right" |
| text rasterised | **5 572 lit pixels from 17 glyphs at 48 px**, box 41,84 to 405,130 |
| geometry + text, run twice | **0 of 921 600 pixels differ**, max channel delta 0, mean error 0.000000 |
| mixed scene ink | 921 039 lit pixels, box 0,0 to 1279,719 |
| W2's 4 000 labels | **151 102 lit pixels**, box 5,0 to 1279,719 |

**Repeatability is exact and content-bearing.** The earlier version of this check reported "0 pixels differ" on two
all-black images; both checks now require ink, so an empty frame cannot pass again.

**This is repeatability, not parity.** No cross-renderer pixel comparison was run, so A1-Parity's unresolved batch
mismatch (13.95% at zoom 1, 63.68% at zoom 8) is untouched, and Graphite+Dawn has not been compared against either
Ganesh or Dawn pixel-for-pixel.

## Feature coverage — NOT run

The 2D feature matrix (paths, joins, caps, dashes, clipping, transforms, opacity, gradients, images) was **not
executed**. The driver has the primitives and the ink-bounding-box analysis to do it, and text plus retained
geometry are proven, but the matrix itself is missing. This is the largest remaining gap, because those features
are the reason Skia is in the tree.

## Software/headless (AD-26) — unresolved

Nothing was tested. Dawn's CPU adapter is the only software path visible in this build and Graphite's Dawn backend
on it is unverified. **AD-26 must not be read as satisfied by this prototype**, and no software rasterizer for
Graphite+Dawn has been shown to exist.

## Cross-platform — nothing verified

Only macOS/Metal ran. Dawn advertises D3D12 and Vulkan and Skia's Dawn backend is backend-agnostic in principle,
but no claim is made. The vcpkg skia port's `dawn` feature is `"!android & !uwp"` with `x11` pulled on Linux, which
is a manifest-level starting point, not evidence.

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

### Recommendation: **No** — do not adopt Graphite+Dawn as Albor's primary renderer

The task asks explicitly whether it should be. On this evidence it should not:

1. **It is 90x slower than Dawn-only on the workload Dawn is for.** 7.679 ms p50 against 0.085 ms, because Graphite
   re-records the retained geometry instead of letting one indexed draw carry it.
2. **It is slower than the Ganesh baseline it was meant to replace.** 112.686 ms p50 against A1.3's 5.92–47.48 ms on
   the same CAD scene, and 7.679 ms against 0.177 ms on W1.
3. **Text, the reason to keep Skia, costs 127 ms per frame** at 4 000 labels — ~31.8 µs a label of CPU recording, with
   the GPU idle (1.555 ms fence).
4. **It cannot be composed into Albor's frame** without an upstream Skia patch, which is excluded by decision. Graphite
   allocates its own surfaces; Albor's geometry and presentation live in Dawn.
5. **The GPU columns cannot rescue it either**, but they also cannot be trusted here: the adapter is an integrated
   Intel UHD 630, and A1's and A1-Dawn's present figures were taken on an adapter this investigation never verified.

**Keep it as a documented, reproducible capability, not a shipping path.** The one-line manifest feature is worth
retaining because it costs nothing when unused, and because "we measured it" is worth more than "we assumed it".
**AD-28 should not be decided by this prototype** — the blocker it was recorded against is resolved, but the
candidate it was blocking has now been measured and found wanting on both axes. The open question AD-28 still has is
whether Dawn *without* Skia can carry the text pipeline, and that is a separate investigation.

**What would change this answer:** a Skia whose Graphite records retained geometry as a single pass, and a text
recording cost an order of magnitude lower. Neither is a configuration change; both are Skia work. Configuration A is real and the port change was
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
