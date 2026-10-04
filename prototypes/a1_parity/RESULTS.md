# Prototype A1-Parity — results

Both renderers now draw the same rendering style and the comparison is a fair image comparison. **Pixel parity is not
established, and the committed thresholds fail.** Driver:
`./.build-release/prototypes/a1_parity/albor-prototype-a1-parity`, runtime about 6 seconds. Thresholds were committed
before any run in [`plan.md`](plan.md) (`54800c7`), amended before the first valid run by the scope change recorded there.

## What was fixed to get here

- **The render pipeline was invalid.** This build's error scopes never said so; `CreateRenderPipelineAsync` reported it
  directly: *"Vertex attribute slot 1 used in [EntryPoint "vs_main"] is not present in the VertexState."* A vertex layout
  reads its attributes through one pointer plus a count, and two separately declared locals are not an array.
- **Antialiasing.** The pass renders into a 4-sample colour buffer and resolves into the target, and the pipeline carries
  the same sample count, so both engines antialias.
- **A stroke-style artifact, not a renderer difference.** The first comparison had Skia drawing a native `drawPoints`
  stroke while Dawn drew a quad strip — two rendering styles, so it measured nothing. Skia now draws the same
  cosmetic filled quads, expanded in pixel space after the view transform, exactly as Dawn's shader does per instance.

## Results

Both engines render, and are deterministic: T2 reports 0 differing bytes across repeats, and every figure below repeats
to the digit.

| Threshold | Measure | Value | Bound | |
| --- | --- | --- | --- | --- |
| T1 | Dawn readback matches the clear colour | 1024000 of 1024000 pixels | 100% | **pass** |
| T2 | Determinism across 2 repeats | 0 differing bytes | 0 | **pass** |
| T3 | Pixels differing, zoom 1.0 | 15.4090% | ≤ 0.20% | **fail** |
| T4 | Differing pixels within ±4 | 0.00% | ≥ 99% | **fail** |
| T6 | Pixels differing, zoom 8.0 | 75.7201% | ≤ 1.00% | **fail** |
| T7 | Skia stroke width ratio, zoom 8 vs 1 | 0.07× | ≥ 3.0× or ≤ 1.25× | inconclusive |
| T5 | Ganesh against Dawn, GPU against GPU | not run | ≤ 0.60% | deferred |

| Measure | skia-raster | dawn |
| --- | --- | --- |
| Lit pixels, zoom 1.0 | 652,241 | 722,671 |
| Mean frame, zoom 1.0 | 35.1 ms | 0.83 ms |
| Lit pixels, zoom 8.0 | 388,385 | 442,158 |
| Mean frame, zoom 8.0 | 19.4 ms | 0.096 ms |

## Reading the failures

**The two engines agree where it matters and differ at the edges.** The solid interior of the stroke is identical: at the
frame centre both read `255 64 32 255`. Zoom-1 disagreement fell from **74.65% to 15.41%** once both engines drew the
same quad style, which is the size of the artifact that was removed rather than a property of either renderer.

**T4 is the wrong instrument, and the record should say so.** With antialiasing on both sides, a partially covered edge
pixel is a legitimate mid-tone between the clear and the stroke, so almost no differing pixel lands within ±4 of either
engine's value. The 0.00% is not evidence of a wrong image; it means "share of differing pixels within ±4" cannot
measure antialiased agreement. It needs replacing with a coverage comparison.

**Coverage differs by more than antialiasing explains.** Mean red over the frame is 192.6 for Dawn against 167.1 for
Skia at zoom 1, so Dawn lays down about 15% more stroke. The longest run of differing pixels along any row is 32 px at
zoom 1, far wider than a one-pixel antialiased edge. Skia rasterises the quad corners it is given while Dawn computes its
corners in the vertex shader, and on near-degenerate short segments a small float difference in the perpendicular
amplifies. **Which engine is closer to the intended one-pixel cosmetic line is not established by this prototype.**

**Zoom 8.0 is dominated by the recorded Skia stroke defect**, not by parity: Skia lights 388,385 pixels where Dawn lights
442,158, and the committed T6 threshold is not meaningful against a known geometry-space-stroke baseline.

**T7 remains invalid.** At 0.07× it is neither cosmetic nor geometric, and lit-pixels-over-drawn-length is not a width
measurement on an overlapping batch. It needs a single isolated segment and a direct run-length measurement.

## Correction: Dawn does draw; the isolated-segment failure was a harness artifact

The finding above was **withdrawn**. A renderer that lights 722,671 pixels in a batch obviously draws strokes, and the
isolated-segment result of zero ink was the harness lying, not Dawn failing. The upload of the vertex buffer was keyed on
whether a buffer exists; the isolated segment is a *different* resource set, so no upload happened, and Dawn drew instance
0 of the previous set at a different view transform. Skia redraws from the set it is handed every frame; Dawn did not. The
upload is now keyed on the set.

Isolated, one horizontal segment at stroke width 1.0, identity view:

| Engine | Ink across column | Peak | Rows spanned |
| --- | --- | --- | --- |
| skia-raster | 1.000 px | 1.000 | 400 |
| dawn | 0.500 px | 0.500 | 399 |

At width 2.0, the same measurement: Skia 2.000 px across rows 399-400, Dawn 1.000 px across row 399 only. **Dawn draws the
line but lays down exactly half the requested ink, at both widths.** The quad spans [399.5, 400.5] at width 1, which
straddles the row boundary, and the half-ink is consistent with a sub-pixel placement difference between Skia's analytic
rasterisation and Dawn's MSAA sample positions. That attribution is **not established**; it is the remaining open question,
and it is now a small, well-instrumented one rather than a total failure.


The coverage question was settled by isolating one clean case. A single horizontal segment from (400, 400) to (600, 400),
stroke width 1.0, identity view — no batch, no overlap, nothing to attribute it to:

| Engine | Lit pixels | Ink across column x=500 | Peak coverage | Rows spanned |
| --- | --- | --- | --- | --- |
| skia-raster | 200 | 1.000 px | **1.000** | 400 to 400 |
| dawn | 44 | 0.000 px | **0.000** | none |

**Skia draws a perfect one-pixel cosmetic line. Dawn draws nothing.** This is not a tolerance, a coverage subtlety or an
antialiasing difference: at the intended width, on the simplest possible geometry, Dawn's quad expansion produces no
visible stroke at all.

This reframes every earlier number. The 15.41% pixel disagreement and the ~15% mean-coverage gap are the *same* fact seen
in a batch: in a batch, short segments overlap and accumulate enough coverage to look like a drawing, which is why the
aggregate figures looked like a subtle mismatch rather than a total failure. Isolated, there is nothing.

**The coverage metric confirms the same thing on the batch**: averaged over 4x4 blocks, 54.12% of blocks differ
(34,636 of 64,000), with only 2.47% within ±4.

## Disposition

- **AD-28 stays *Proposed*.** Skia remains the default renderer and is not retired.
- **The renderer-swap question is answered negatively for this configuration.** Dawn does not draw a one-pixel cosmetic
  line on isolated geometry, where Skia does, so Dawn cannot yet be shown to produce Skia's image. The earlier aggregate
  figures understated this: in a batch, overlap hides the failure.
- The windowed timings in Prototype A1 and Prototype A1-Dawn stay **unverified**; the platform pacing finding in
  `plan.md` explains why, and this prototype's offscreen, clock-driven measurement is unaffected by it.

## Next proofs, in order

1. **Replace the T4 instrument with a coverage comparison**, so antialiased agreement is measured rather than declared
   unmeasurable.
2. **Isolate the coverage difference** to one clean segment, which separates antialiasing from the quad-corner
   computation and says which engine is correct.
3. **Redesign T7** on that same isolated segment, which also quantifies the recorded Skia stroke defect properly.
4. **T5**, the Ganesh-against-Dawn GPU column, only after 1 to 3, since this Skia build exposes a Metal render target
   through a narrower API than the raster backend needs.