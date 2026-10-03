# Prototype A1-Parity — results

**This prototype has not produced a result.** It is a harness that currently detects and reports its own failure. No
parity conclusion about Dawn and Skia exists, and nothing in `albor-design-v3.md` changes because of this file.

Driver: `./.build-release/prototypes/a1_parity/albor-prototype-a1-parity`. Runtime: about 5 seconds.
Thresholds were committed before any run in [`plan.md`](plan.md) (`54800c7`), then amended before the first *valid* run
by a scope change that is recorded in that file.

## Why there is no result

Two independent defects, both found by the harness's own thresholds rather than by inspection.

### T1 fails: the Dawn readback does not return the rendered frame

The sanity check asks Dawn to draw a frame with no geometry, so the target is nothing but the clear colour, and then
reads it back. **0 of 1,024,000 pixels match the clear colour**, at a tolerance of one 8-bit step.

What was ruled out, in order:

- The draw was not issued at all — ruled out. An empty request used to return before issuing any work; that was fixed,
  and the clear is now issued unconditionally.
- Dawn rejected the pipeline, the descriptor or the draw call — ruled out. A validation error scope now brackets the
  pipeline build and the first frame, and nothing is reported.
- Dawn rejected the texture-to-buffer copy — ruled out. A validation error scope brackets the first copy too, and
  nothing is reported.
- The map never completed — ruled out. The map callback reports success, and `GetMapState()` reports `Mapped` (3) with
  the buffer size equal to the size requested.
- `GetMappedRange()` returns a null pointer — **this is where it stands.** With the buffer mapped and the size correct,
  the pointer is null, so the slice reads through `ReadMappedRange()` instead, which reports success.

So the copy is accepted, the map succeeds, the read succeeds, and the bytes are still not the clear colour. The two
remaining explanations are that the copy is a silent no-op in this Dawn build for a buffer this size, or that the bytes
come back in a different channel order than the texture was written (a BGRA readback against an RGBA texture would
score exactly 0 of 1,024,000 at a one-step tolerance). Neither has been distinguished. **Not resolved.**

T5 is separately deferred, not weakened: comparing Ganesh against Dawn needs a Metal render target that this Skia
(milestone 148) exposes only through a narrower API, so the GPU-against-GPU column is unevaluated rather than
approximated by the CPU backend and passed off as a GPU result.

### T2 fails: Skia's own output is not reproducible between repeats

Repeat 2 differs from repeat 1 at **953,470 of 1,024,000 pixels**, while the lit-pixel count is *identical* in both
(29,593 at zoom 1.0). Identical foreground with a different background across two runs of a CPU rasteriser with a
fixed seed is not plausible as renderer behaviour, so this is a defect in the harness — most likely in how the
reference buffer from repeat 1 is retained or compared — and not a property of Skia. **Not resolved.**

## What was measured, for the record

Neither row below is a parity result; they are what the harness printed while failing.

| Engine | Frames | Mean frame | Slowest frame | Lit pixels at zoom 1.0 |
| --- | --- | --- | --- | --- |
| skia-raster | 9 timed | 105.5 ms | 109.8 ms | 29,593 |
| dawn | 9 timed | 0.011 ms | 0.015 ms | 0 |

Skia's ~105 ms per frame at 20,000 segments is the CPU stroke path (`drawPoints`), which is the same path A1.3 recorded
at 10,470 ms for 1.8 × 10⁶ segments. Dawn's 0.011 ms is a submission cost only and says nothing about rasterisation,
because nothing was rasterised.

## Disposition

- **AD-28 stays *Proposed*.** Skia remains the default renderer and is not retired.
- The recorded windowed timings in Prototype A1 and Prototype A1-Dawn stay **unverified** and are not repaired by this
  file. This prototype's `plan.md` records the standalone `CADisplayLink` evidence that they cannot currently be
  reproduced.
- T7 was not evaluated: it depends on buffers this run could not trust.
- The next step is to resolve the Dawn readback, most cheaply by writing a target whose four channels differ widely and
  reading back one pixel, which distinguishes a silent no-op from a channel-order difference in a single run.