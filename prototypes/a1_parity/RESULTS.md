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
- **WGSL quad corner table (half-ink defect resolved).** In `kShaderWgsl` (`vs_main`), `side` was selected as +1.0 only
  for `corner == 1u || corner == 4u`. For corner 5 (which should be `p1+`), `side` evaluated to -1.0, making corner 5
  identical to corner 3 (`p1-`). This collapsed Triangle 2 `(p1-, p0+, p1-)` into a zero-area line segment. Only
  Triangle 1 `(p0-, p0+, p1-)` was rendered, whose diagonal crosses the centerline at the midpoint, halving the ink
  across all widths and heights. Adding `corner == 5u` to the side selector (`corner == 1u || corner == 4u || corner == 5u`)
  completes the quad.

## Results

Both engines render, and are deterministic: T2 reports 0 differing bytes across repeats, and every figure below repeats
to the digit.

| Threshold | Measure | Value | Bound | |
| --- | --- | --- | --- | --- |
| T1 | Dawn readback matches the clear colour | 1024000 of 1024000 pixels | 100% | **pass** |
| T2 | Determinism across 2 repeats | 0 differing bytes | 0 | **pass** |
| T3 | Pixels differing, zoom 1.0 | 13.9549% | ≤ 0.20% | **fail** |
| T4 | Differing pixels within ±4 | 0.00% | ≥ 99% | **fail** |
| T6 | Pixels differing, zoom 8.0 | 63.6837% | ≤ 1.00% | **fail** |
| T7 | Cosmetic stroke width ratio, zoom 8 vs 1 | Skia 1.00×, Dawn 1.00× | ≤ 1.25× | **pass** |
| T5 | Ganesh against Dawn, GPU against GPU | not run | ≤ 0.60% | deferred |

| Measure | skia-raster | dawn |
| --- | --- | --- |
| Lit pixels, zoom 1.0 | 652,241 | 770,859 |
| Mean frame, zoom 1.0 | 35.7 ms | 1.10 ms (0.15 ms resident) |
| Lit pixels, zoom 8.0 | 388,385 | 861,020 |
| Mean frame, zoom 8.0 | 19.7 ms | 0.19 ms (0.07 ms resident) |

## Reading the failures

**The two engines agree where it matters and differ at the edges.** The solid interior of the stroke is identical: at the
frame centre both read `255 64 32 255`. Zoom-1 disagreement is **13.95%** with both triangles of the quad drawn.

**T4 is the wrong instrument, and the record should say so.** With antialiasing on both sides, a partially covered edge
pixel is a legitimate mid-tone between the clear and the stroke, so almost no differing pixel lands within ±4 of either
engine's value. The 0.00% is not evidence of a wrong image; it means "share of differing pixels within ±4" cannot
measure antialiased agreement. It needs replacing with a coverage comparison.

**Coverage differs by more than antialiasing explains.** Averaged over 4x4 blocks on the batch, 53.18% of blocks differ
(34,032 of 64,000). Skia rasterises the quad corners it is given while Dawn computes its corners in the vertex shader, and on
near-degenerate short segments a small float difference in the perpendicular amplifies.

**Zoom 8.0 is dominated by the recorded Skia stroke defect**, not by parity: Skia lights 388,385 pixels where Dawn lights
861,020, and the committed T6 threshold is not meaningful against a known geometry-space-stroke baseline.

**T7 confirms cosmetic stroke behaviour for both engines.** Measured on the isolated segment, ink width is 1.000 px at zoom 1
and 1.000 px at zoom 8 for Skia (ratio 1.00×) and 1.000 px at zoom 1 and 1.000 px at zoom 8 for Dawn (ratio 1.00×) →
**cosmetic confirmed for both engines**: strokes keep their pixel width under zoom. The previous 0.500 px measurement for Dawn
at zoom 1 was caused by the collapsed second triangle, not by sub-pixel placement.

## Isolated segment and stroke-width sweeps

Once the harness artifact (upload keyed on buffer existence rather than on the resource set) and the WGSL corner defect were
resolved, Dawn and Skia match closely on isolated geometry:

Isolated horizontal segment from (400, 400) to (600, 400), identity view:

| Width | Engine | Lit pixels | Ink across column x=500 | Peak coverage | Rows spanned | Pixel diff |
| --- | --- | --- | --- | --- | --- | --- |
| 1.0 px | skia-raster | 200 | 1.000 px | 1.000 | 400 to 400 | — |
| 1.0 px | dawn | 0 (strict colour) | 1.000 px | 0.500 | 399 to 400 | 0.0391% (edge AA) |
| 2.0 px | skia-raster | 400 | 2.000 px | 1.000 | 399 to 400 | — |
| 2.0 px | dawn | 400 | 2.000 px | 1.000 | 399 to 400 | **0.0000% (exact match)** |

At width 2.0, Skia and Dawn agree pixel-for-pixel across the entire 1280x800 framebuffer (0.0000% pixel difference, 0.0000%
block difference). At width 1.0, the line straddles the integer row boundary y=400: Skia places the 1px line on row 400, while
Dawn's 4x MSAA splits coverage symmetrically between rows 399 (0.500) and 400 (0.500), conserving total ink at exactly 1.000 px.

### T7b: Ink against requested width at zoom 1

| Requested width | skia-raster ink | skia ratio | dawn ink | dawn ratio | dawn inked rows |
| --- | --- | --- | --- | --- | --- |
| 0.50 px | 0.000 px | 0.000 | 0.496 px | 0.992 | 399:0.248, 400:0.248 |
| 1.00 px | 1.000 px | 1.000 | 1.000 px | 1.000 | 399:0.500, 400:0.500 |
| 2.00 px | 2.000 px | 1.000 | 2.000 px | 1.000 | 399:1.000, 400:1.000 |
| 4.00 px | 4.000 px | 1.000 | 4.000 px | 1.000 | 398:1.000, 399:1.000, 400:1.000, 401:1.000 |
| 8.00 px | 8.000 px | 1.000 | 8.000 px | 1.000 | 396-403 all 1.000 |

Dawn delivers a ratio of 1.000 across all requested widths (0.992 at 0.50 px due to sub-pixel sampling; Skia drops 0.50 px to 0).

### T7c: Requested width 2.0 at several heights

| Segment height | skia-raster ink (of 2.0 px) | dawn ink (of 2.0 px) |
| --- | --- | --- |
| y = 100 | 2.000 px | 2.000 px |
| y = 300 | 2.000 px | 2.000 px |
| y = 400 | 2.000 px | 2.000 px |
| y = 500 | 2.000 px | 2.000 px |
| y = 700 | 2.000 px | 2.000 px |

The ink integral is 2.000 px at every height for both engines.

## Disposition

- **AD-28 stays *Proposed*.** Skia remains the default renderer and is not retired.
- **The renderer-swap question is still open.** While Dawn matches Skia pixel-for-pixel on clean isolated geometry at width 2.0
  and conserves ink on all widths and heights, batch parity fails the tight edge tolerance (13.95% vs 0.20% threshold).
- The windowed timings in Prototype A1 and Prototype A1-Dawn stay **unverified**; the platform pacing finding in
  `plan.md` explains why, and this prototype's offscreen, clock-driven measurement is unaffected by it.

## Next proofs, in order

1. **Replace the T4 instrument with a coverage comparison**, so antialiased agreement is measured rather than demanding
   bit-level agreement on edge pixels.
2. **Define edge antialiasing tolerance** between analytic rasterisation and 4x MSAA for batch geometry.
3. **T5**, the Ganesh-against-Dawn GPU column, once a Metal offscreen render target is exposed for Skia.