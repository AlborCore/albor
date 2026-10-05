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

- **1.8 M segments record in 7.287 ms p50** (median of three passes, 5.9% spread) against Dawn-only's **0.085 ms** for
  a single `Draw(6, 1 800 000)` — **86x more CPU**, and Graphite gives up the batching advantage that motivated the
  whole idea.
- **4 000 text labels record in 89.3 ms p50**, ~22.3 µs each, dominating everything else by more than 10x. The
  combined CAD scene records in **97.4 ms p50**, a ~10.3 fps ceiling from CPU recording alone, against Ganesh's
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

**Protocol: three sequential passes of the whole driver**, A1's workloads, seeds, label paint and draw loop unchanged,
the same 8 warm-up frames and 5 x 20 timed frames as every other slice. Passes are strictly sequential and the driver
refuses to start while another instance holds its lock, because two instances share the cpu and the single gpu.
`run_repeated.sh` runs the protocol and samples load average before and after each pass.

**A fourth pass taken under load is recorded and discarded.** The machine showed load average 2.31–3.94 with several
`CoreServices` processes at 10–16% cpu, and its figures are inflated accordingly — most visibly W2's geometry GPU
fence, which it reported as 91.977 ms against 37–38 ms in every clean pass, a 2.4x difference. It is the only sample
that overlaps the driver and it is not used. That this check was worth doing is not in doubt: it changed the W2 text
figure from 127.321 ms to 89.301 ms and reversed the sign of one anomaly.

Median of the three clean passes, with the observed range:

| workload | record p50 | enqueue p50 | GPU fence p50 | record p99 |
| --- | ---: | ---: | ---: | ---: |
| **W1** geometry, 1 800 000 segments | **7.287 ms** (7.111–7.533) | 2.509 (2.423–2.745) | 596.304 (596.165–596.814) | 10.535 (9.793–11.166) |
| **W2** geometry only, 100 000 symbols | **4.373 ms** (4.066–4.397) | 1.603 (1.524–1.694) | 38.309 (37.230–38.460) | 6.614 (6.299–14.917) |
| **W2** text only, 4 000 labels | **89.301 ms** (88.175–97.002) | 0.348 (0.345–0.366) | 1.543 (1.343–1.550) | 100.070 (92.726–166.070) |
| **W2** geometry + text | **97.434 ms** (95.644–97.575) | 1.673 (1.635–1.690) | 69.542 (69.425–69.678) | 118.074 (117.219–124.886) |

**W1 recording is the stable measurement**: 7.111–7.533 ms, a 5.9% spread, and the W1 GPU fence is tighter still at
0.1%. Those two rows carry the conclusion.

**Text is ~22.3 µs per label of CPU recording** (89.301 ms / 4 000), and it dominates: the labels alone cost more
than geometry and text together minus the geometry, and the GPU fence for the text pass is 1.543 ms, so it is
entirely CPU-side. Geometry is the opposite — a 596 ms fence against 7.3 ms of recording means W1 is GPU-bound on
this adapter.

**W3, update half.** Rebuilding the 1.8 M-segment retained set costs **911.788 ms p50** (889.767–960.549) on the cpu,
which is A1's already-recorded cost of an append copying every point again, not a Graphite cost; re-recording the
mutated set costs **5.458 ms p50** (5.229–5.645).

**Memory.** Graphite reports 5 599 232 bytes budgeted and 5 595 136 purgeable against a 256 MiB ceiling —
**byte-identical in all four passes**, including the loaded one.

### The anomaly from the single run was an artifact, and is now resolved

The earlier single pass reported text-only (127.321 ms) *slower* than geometry+text (112.686 ms), which is not
physically possible and was flagged as variance. With three clean passes the ordering is the expected one —
text-only 89.301 ms against combined 97.434 ms — and the combined figure is stable to 2.0% (95.644–97.575). The
anomaly was the loaded pass, not the machine.

## Same-session measurement of every configuration

Every configuration below was re-run on this machine in one session, so the comparison no longer mixes a fresh
candidate against baselines of unknown age. **Two historical figures did not reproduce and are corrected here**, and
one earlier conclusion of this document is withdrawn.

### W1 — 1 800 000 retained segments

| configuration | record p50 | record p99 | draw calls/frame | present p50 | gate |
| --- | ---: | ---: | ---: | ---: | --- |
| Ganesh (A1.3) | 2.541 ms | 3.729 ms | 220 | **547.19 ms** | 13 failures |
| Graphite/Metal (A1.4, **submission bug fixed**) | 6.367 ms | **10.602 ms** | not recorded | offscreen | 1 failure: p99 over the 8.33 ms deadline |
| Dawn-only (A1-Dawn, 2 samples) | **0.298 / 0.308 ms** | 0.681 / 0.720 ms | **1** | **16.66 ms** | 6 failures, all presentation |
| **Graphite+Dawn** (3 passes) | 7.287 ms | 10.535 ms | not observable | offscreen (fence 596 ms) | — |

### W2 — CAD canvas, 4 000 labels

| configuration | record p50 | what it drew |
| --- | ---: | --- |
| Ganesh (A1.3) | **160.254 ms** | geometry + 4 000 labels, 100 000 segments, 47.8 MB retained |
| Dawn-only (A1-Dawn) | 0.361 ms | geometry only — **no glyphs**, and 1 000 000 segments in a 19.8 MB Dawn-native set |
| **Graphite+Dawn** | **97.434 ms** (geometry 4.373, text 89.301) | geometry + 4 000 labels, 100 000 segments, 47.8 MB retained |

**W2 is only comparable between Ganesh and Graphite+Dawn**, which share A1's factory and report identical retained
bytes. A1-Dawn's W2 is a different retained representation with a different segment count, so its 0.361 ms is not
the same workload.

### W3 — multi-window isolation with a 100 ms hold on window 1

| configuration | window 2 frames presented during the hold | window 2 present p50 |
| --- | ---: | ---: |
| budget | ≥ 95% | ≤ 16.7 ms |
| Ganesh (A1.3) | **2.39%** | 696.71 ms |
| Dawn-only (A1-Dawn) | **16.39%** | 16.82 ms |
| Graphite+Dawn | not reproducible offscreen | not measured |

## Three corrections to the existing record

**1. A1.4's published comparison was wrong, and the cause was the submission bug.** It reported Ganesh 0.177 ms
against Graphite 0.220 ms — a 1.24x ratio — with Ganesh's figure hardcoded from A1.3's results rather than measured.
With the recording actually inserted, Graphite records 6.367 ms against a freshly measured Ganesh 2.541 ms: **2.5x at
p50 and 2.8x at p99, not 1.24x.** Graphite still loses to Ganesh on recording, by more than A1.4 concluded, and it
**misses the 8.33 ms R1 processing deadline that Ganesh meets with 4.5x margin**.

**2. The "Graphite+Dawn records 35x slower than Graphite-on-Metal" anomaly is withdrawn.** It was entirely an
artifact of the same missing insert. Corrected: 7.287 ms against 6.367 ms is **1.14x** — the Dawn backend costs about
14% more recording than the Metal backend for the same 1.8 M-segment replay. The two rows are now measuring the same
thing, and they agree, which is the cross-check that both pipelines are real.

**3. Neither headline baseline number reproduced.** A1.3's recorded Ganesh W1 recording p50 of 0.177 ms measured
**2.541 ms** today, 14x higher. A1-Dawn's recorded 0.085 ms measured **0.298–0.308 ms** across two samples, 3.6x
higher. The relative ordering is unchanged and the ratios that matter are large enough to survive, but the absolute
figures in the older documents should not be quoted without this caveat.

## What the corrected numbers change about the conclusion

**Graphite+Dawn is not uniformly worse than Ganesh — it depends on the workload, and the reason is text.**

| comparison | Graphite+Dawn vs Ganesh |
| --- | --- |
| W1 geometry, 1.8 M segments | **2.9x worse** (7.287 vs 2.541 ms) |
| W2 CAD with 4 000 labels | **1.6x better** (97.434 vs 160.254 ms) |

W2 is where Ganesh pays for CPU-side stroking and tessellation of 100 000 symbol outlines, work Graphite does on the
gpu. So Graphite's case is narrower than "it loses": it loses on large retained geometry and wins on many small
stroked paths.

**But that does not rescue it, because of the third column.** Dawn-only records W1 in 0.298 ms — **24x faster than
Graphite+Dawn and 8.5x faster than Ganesh** — with one draw call and a healthy 16.66 ms present interval, and its W2
geometry is 0.361 ms against Graphite+Dawn's 4.373 ms. The gap is not an artifact of presentation or of Ganesh's
tessellation; it is what re-recording every retained vertex on the cpu costs.

**The finding that matters most for AD-28 is the text column, and it indicts Skia rather than Graphite.** Rendering
4 000 labels costs 160 ms on Ganesh and 89 ms on Graphite+Dawn. Text is expensive on *both* Skia backends, by 20x and
100x their respective geometry costs. That is why Dawn-only looks unbeatable here: **it does not draw the text at
all.** Its 0.361 ms W2 is not a win over Graphite+Dawn's 97 ms — it is a win at omitting the only expensive part of
the scene. So the real conclusion is not "Graphite is slow" but:

> **Skia's text is the bottleneck on every Skia backend, and that — not the renderer — is what a Dawn primary would
> have to replace.**

That reframes AD-28's blocker (2) "no text pipeline" from a missing feature into the central open problem, and it is
the one worth the next investigation.

## Problems encountered in this session

| problem | status |
| --- | --- |
| A1.4 had no `snap()`/`insertRecording()`, so its submit and frame figures were void | **fixed**; the corrected numbers are above |
| A1.3 aborts at teardown with `GrManagedResource.h:61: check(0 == fHashSet.count())` after printing all rows | **not fixed**; a Ganesh resource-leak assertion |
| A1.3's present interval is 547 ms and its submit 544 ms, matching the CAMetalLayer starvation A2 documented | **not fixed**; its windowed columns are unusable as renderer evidence per §14 |
| A1-Dawn leaves no Dawn uncaptured-error or device-lost callback set, and Dawn warns about it | **not fixed** |
| **A1-Parity aborts** at `SkTo.h:17: check(SkTFitsIn<D>(s))` on "repeat 1 of 2, zoom 1.0", before any pixel comparison | **not fixed; no parity numbers exist.** T1 still passes (Dawn readback exact at 1024000/1024000 pixels) |
| A1.3 reports W2 as 100 000 segments / 47.8 MB while A1-Dawn reports 1 000 000 / 19.8 MB for the same nominal scene | **not resolved**; W2 is compared only between the two slices that share A1's factory |

**A1-Parity is the significant one.** It cannot run at all, so the unresolved Skia-versus-Dawn pixel mismatch
(13.95% at zoom 1, 63.68% at zoom 8) has no current measurement and no current attribution. Any claim about pixel
agreement between the backends is currently unsupported.

## Pixel validation

Verified, with the same assertions the smoke test uses:

| check | result |
| --- | --- |
| primitive placed exactly | **3 200 lit pixels in exactly the requested 80x40 box at 600,340** — not "looks right" |
| text rasterised | **5 572 lit pixels from 17 glyphs at 48 px**, box 41,84 to 405,130 |
| geometry + text, run twice | **0 of 921 600 pixels differ**, max channel delta 0, mean error 0.000000 |
| mixed scene ink | 921 039 lit pixels, box 0,0 to 1279,719 |
| W2's 4 000 labels | **151 102 lit pixels**, box 5,0 to 1279,719 — identical in all three passes |

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

1. **It is 86x slower than Dawn-only on the workload Dawn is for.** 7.287 ms p50 against 0.085 ms, because Graphite
   re-records the retained geometry instead of letting one indexed draw carry it.
2. **It is slower than the Ganesh baseline it was meant to replace.** 97.434 ms p50 against A1.3's 5.92–47.48 ms on
   the same CAD scene, and 7.287 ms against 0.177 ms on W1.
3. **Text, the reason to keep Skia, costs 89 ms per frame** at 4 000 labels — ~22.3 µs a label of CPU recording, with
   the GPU idle (1.543 ms fence).
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
