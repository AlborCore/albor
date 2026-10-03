# Prototype A1-Parity — renderer parity under a deterministic clock

Recorded plan for Prototype A1-Parity (§30), closing the output-parity gap left by Prototypes A1 and A1-Dawn
(AD-28). Prototype code: not linked by any runtime module (AGENTS.md).

## Why this prototype exists

Prototypes A1 and A1-Dawn each recorded *timings* and never compared *pixels*. Two renderers were measured against the
same workloads, the same seeds and the same thresholds, and nobody ever put their output side by side. That gap is
load-bearing for AD-28: the architecture proposes Dawn as the candidate HAL, and a renderer swap is only defensible if
the two renderers produce the same image for the same input.

Two recorded claims cannot be checked without this prototype:

1. **The 99.41% identical hover hit ratio is not output parity.** It is a CPU-side geometry comparison over a uniform
   grid. Identical inputs proved, nothing about identical pixels.
2. **The Skia stroke defect is unquantified.** A1-Dawn recorded that Skia's pre-tessellated quads fix stroke width in
   geometry space, so strokes scale with zoom, while Dawn expands quads in pixel space and stays cosmetic. The claim is
   asserted and never measured.

## Why the clock is deterministic

Every windowed measurement in A1 and A1-Dawn is currently **unreproducible**, and that is a finding, not a
inconvenience. A standalone 60-line program with no albor code, run on this machine (macOS 26.6.2, Darwin 25.6.0),
drives a `CADisplayLink` at 58 Hz or at exactly 0 Hz, apparently at random, when a `CAMetalLayer` is attached:

| Setup | Tick rate |
| --- | --- |
| small window, no layer | 58.0 Hz |
| small + `CAMetalLayer` attached | 57.6 Hz |
| small + layer + device, no drawableSize | **0.0 Hz** |
| small + layer + device + drawableSize | 53.7 Hz |
| large, no layer | **0.0 Hz** |
| large + layer, no device | 58.0 Hz |
| large + layer + device, no drawableSize | **0.0 Hz** |
| large + layer + device + drawableSize | 54.6 Hz |

The distribution is bimodal and independent of the flags. A2's design — a `CAMetalLayer` paced by a `CADisplayLink` —
sits exactly in the failing configuration, which is why both prototypes hang identically and why their windowed timings
cannot be reproduced on demand.

A parity prototype that depends on that link would be a parity prototype that reports nothing half the time. So frames
are driven by a deterministic loop and rendered to offscreen targets. **What this can and cannot measure is stated
before any run, not after:** it measures whether two renderers produce the same image, and what each costs to produce
it off the display. It does **not** measure vsync-limited presentation cadence. That question stays open.

## Dependency

This prototype links **both** prior prototypes and adds nothing new to them:

- `albor-prototype-a1` for the Skia renderer and its include root.
- `albor-prototype-a1-dawn` for Dawn, and for `resources.h` — the renderer-neutral geometry generator that both
  prior prototypes already share, which is what makes byte-identical input structural rather than a convention.
- Neither prior prototype's source is modified. Every new file is in this folder.

Fairness is enforced by construction, not by discipline: one `ResourceSet` is generated once and both renderers are
handed the same immutable object, the same view transform, the same clip, the same pixel size and the same clear colour.

## Scope change, recorded before any valid run

The first two attempts are **not** measurements and are not counted as runs. Both were diagnostic: dawn drew nothing,
because the pipeline and the readback were still being brought up, so every parity figure they produced was
meaningless. Two things were fixed as a result, and both are recorded here because they change the workload:

1. **An empty request now clears the target.** It previously returned before issuing any work, which is why the readback
   sanity check T1 failed for a reason that had nothing to do with the readback.
2. **The workload is smaller.** The first attempt used 200,000 segments and 60 frames per engine and took about seven
   minutes, almost all of it skia's cpu stroke path at roughly 1.1 seconds per frame. A parity check that takes seven
   minutes is not a parity check, so the committed workload is reduced to **20,000 segments and 10 frames per engine**,
   and the recorded run is **2 repeats**.

No threshold value is relaxed by this change. T1 through T4, T6, T7 and T8 keep the values committed in `54800c7`.
T5 is **deferred, not weakened**: comparing ganesh against dawn needs a metal render target that this skia build
(milestone 148) only exposes through a narrower api, so the gpu-against-gpu column is left unevaluated rather than
approximated by the cpu backend and presented as a gpu result.

## Reference machine

Intel Core i7-9750H @ 2.60 GHz, 16 GB, macOS 26.6.2 (Darwin 25.6.0), `release` preset. Recorded before any run, as
§28.4 requires. The machine's display-link behaviour is recorded above as a **known unreliable input**.

## Workload

One workload, **P1**, at a size that keeps a readback cheap:

- 200,000 line segments, extent 1000.0, seed 20250114 — the same generator, seed and call order A1.3 used, at a
  segment count small enough that three repeats of three renderers finish quickly.
- 1280 × 800 pixels, clear colour `(0.05, 0.06, 0.08, 1.0)`, identical to A2's clear.
- Two view transforms: **zoom 1.0** and **zoom 8.0**, both offset to frame the same content.
- 60 frames per engine per run, driven by a deterministic loop.

Three renderers are compared, and the third is the point of the third:

1. **Skia raster (CPU)** — the deterministic reference for geometry.
2. **Skia Ganesh (Metal, offscreen)** — the GPU renderer A1.3 measured, offscreen.
3. **Dawn (Metal, offscreen)** — the candidate AD-28 proposes.

## Thresholds

Recorded **before** any run (§28.4). A failed threshold is reported as failed and is not renegotiated afterwards.

| # | Threshold | Value |
| --- | --- | --- |
| T1 | Dawn readback sanity: an empty frame reads back the clear colour at every pixel | 100% of pixels |
| T2 | Determinism: each renderer's pixels are byte-identical across 3 repeats | 0 differing bytes |
| T3 | Geometry parity at zoom 1.0, Skia raster vs Dawn | ≤ 0.20% of pixels differing |
| T4 | Geometry parity at zoom 1.0, Skia raster vs Dawn, among differing pixels the share with max channel delta ≤ 4/255 | ≥ 99% |
| T5 | Geometry parity at zoom 1.0, Skia Ganesh vs Dawn | ≤ 0.60% of pixels differing |
| T6 | Geometry parity at zoom 8.0, Skia raster vs Dawn | ≤ 1.00% of pixels differing |
| T7 | **Skia stroke defect**: Skia's rendered stroke width at zoom 8.0, measured in pixels, versus at zoom 1.0 | ≥ 3.0× if strokes scale with geometry (the recorded defect); ≤ 1.25× if cosmetic |
| T8 | Frames completed per engine under the deterministic clock | 60 of 60, 0 dropped |

**T7 is a threshold on a hypothesis, not on quality.** It is recorded as `≥ 3.0×` because A1-Dawn asserted that Skia
scales. If the measurement comes back under 1.25×, the assertion in A1-Dawn's `RESULTS.md` is wrong and that file is
corrected. The threshold is not moved to fit whichever answer arrives.

**T3–T6 are tolerances, not equivalence.** Two rasterisers with different antialiasing are not expected to agree to the
bit; these bounds say the disagreement must stay confined to edges.

## What this prototype will not settle

- **Presentation cadence.** No vsync, no display link, no swap chain. §10.2/§10.3 pacing claims stay blocked on the
  platform, and AD-28's timing half stays *Proposed*.
- **Text.** Dawn draws no glyphs; W2's 4000 labels are still Dawn's missing 30× cost. Unchanged by this prototype.
- **Windows D3D12 and Linux Vulkan.** macOS Metal only.
- **The `CADisplayLink` reliability finding itself.** Recorded above as evidence; measuring it properly (distribution
  over many trials, other machines, other macOS versions) is its own prototype, and until that exists the windowed
  numbers in A1 and A1-Dawn stay unverified.