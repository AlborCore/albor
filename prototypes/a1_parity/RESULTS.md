# Prototype A1-Parity — results

**This prototype has not produced a parity number.** It runs in about 6 seconds and reports its own state honestly: one
threshold now passes, one is blocked on a Dawn defect that is characterised but not fixed. No conclusion about Dawn and
Skia output agreement exists, and nothing in `albor-design-v3.md` changes because of this file.

Driver: `./.build-release/prototypes/a1_parity/albor-prototype-a1-parity`. Runtime: about 6 seconds.
Thresholds were committed before any run in [`plan.md`](plan.md) (`54800c7`), amended before the first valid run by the
scope change recorded in that file.

## T2 passes: both renderers are deterministic

**Skia is byte-identical across repeats**, at both view transforms. 0 differing pixels against its own first repeat.

This is a real result and it corrected a false one. The first version of this prototype reported T2 failing at 953,470
of 1,024,000 pixels with an identical lit-pixel count, which is not possible from a fixed-seed CPU rasteriser. The cause
was in the harness, not in Skia: one reference list held the pixels of **every** view, and the T2 lookup matched on the
engine alone, so a repeat at zoom 8.0 was being compared against a repeat at zoom 1.0. The reference is now keyed on
(engine, scale). A1-Dawn's identical 99.41% hover hit ratio was never affected by this, because it came from a different
mechanism.

## T1 fails: dawn's render pass leaves the target empty

The sanity check asks Dawn to draw a frame with no geometry, so the target is nothing but the clear colour, then reads
it back. **0 of 1,024,000 pixels match the clear colour** at a tolerance of one 8-bit step, and the first pixel reads
`0 0 0 0` — including a zero alpha, which a cleared target cannot have.

### Dawn itself is not at fault

A standalone program, no albor code, linked against the same `libwebgpu_dawn.a`, clears a 1280x800 `RGBA8Unorm` texture
and copies it back:

```
device ok, queue ok
map state 3 (3 = mapped)
GetMappedRange(0, total) = null
ReadMappedRange ok = 1
first pixel = 13 15 20 255, the clear should be about 13 15 20 255
pixel at 400,400 = 13 15 20 255
RESULT: a clear pass round trips
```

So the device, the queue, the copy, the map and the read all work in this Dawn build. It also confirms that
`GetMappedRange` returning null is a property of this port rather than of this prototype, which is why the slice reads
through `ReadMappedRange`.

### What was established about this prototype

| Question | Experiment | Result |
| --- | --- | --- |
| Does the map and read work? | `Queue::WriteBuffer` a `0xAB` pattern, then map and read | **171,171,171,171** — works |
| Does the copy run? | `Queue::WriteTexture` a `0xCD` fill into the target, then copy and read | **205,205,205,205** — works |
| Does the shader compile? | `ShaderModule::GetCompilationInfo` | **no messages at all** — the WGSL is valid |
| Is a render pass issued? | instrumented `BeginRenderPass` | pass handle valid, draws issued |
| Does the texture then hold the clear? | read it back | **0,0,0,0** |

The one thing the standalone does that this prototype does not is **bind a render pipeline and a bind group**. That is
now the whole of the remaining difference, and it is where the bug is. **Not fixed.**

### Two corrections to earlier conclusions in this file

- A geometry-free frame bound a vertex buffer that had never been created. Binding a null vertex buffer invalidates the
  entire render pass, so the clear was silently discarded with it. Fixed.
- An earlier version concluded "Dawn reports no validation errors". **That was wrong and is withdrawn.** A buffer created
  with `size = 0`, invalid by construction, was put through the same error scope and reported nothing either, so this
  build's error scopes are not a usable signal. Every claim above about what Dawn did or did not reject is qualified by
  that, which is why the shader is now checked through `GetCompilationInfo` instead. `report_scoped_errors` carries the
  caveat in a comment.

## Thresholds not evaluated

- **T3, T4, T6** (Skia against Dawn at both transforms) need Dawn pixels, so they are unevaluated rather than failed.
- **T7** is inconclusive at a 0.06× ratio, which is neither cosmetic nor geometric. It depends on buffers this run could
  not trust, and the ratio is also dominated by overlap: at zoom 8.0 a 20,000-segment random batch piles many strokes
  onto the same pixels, so lit-pixel count divided by drawn length is not a width measurement. **The threshold as
  written does not measure what it claims to measure, and needs redesign before it is run again.**
- **T5** is deferred, not weakened: Ganesh against Dawn needs a Metal render target this Skia (milestone 148) exposes
  only through a narrower API, so the GPU-against-GPU column is unevaluated rather than approximated by the CPU backend
  and presented as a GPU result.

## What was measured, for the record

Neither row is a parity result; they are what the harness printed while one threshold was blocked.

| Engine | Frames | Mean frame | Slowest frame | Lit pixels at zoom 1.0 |
| --- | --- | --- | --- | --- |
| skia-raster | 9 timed | 117.1 ms | 145.1 ms | 29,593 |
| dawn | 9 timed | 0.010 ms | 0.014 ms | 0 |

Skia's ~117 ms per frame at 20,000 segments is the CPU stroke path (`drawPoints`), the same path A1.3 recorded at
10,470 ms for 1.8 × 10⁶ segments. Dawn's 0.010 ms is a submission cost only and says nothing about rasterisation,
because nothing was rasterised.

## Disposition

- **AD-28 stays *Proposed*.** Skia remains the default renderer and is not retired.
- The recorded windowed timings in Prototype A1 and Prototype A1-Dawn stay **unverified**. This prototype's `plan.md`
  records the standalone `CADisplayLink` evidence that they cannot currently be reproduced.
- **Next step, cheapest first:** `CopyTextureToBuffer` returning zeros is a defect in the Dawn build or in how this port
  configures the copy, and it is worth one focused attempt against Dawn's own `CopyTextureToBuffer` example before more
  is built on top of it. If it cannot be made to work, the parity question has to move to a surface Dawn can actually be
  read back from, and the plan has to say so before another threshold is written.