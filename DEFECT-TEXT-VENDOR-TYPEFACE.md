# Defect: the vendored text engine cannot issue a drawable typeface

Context: `text/text-harfbuzz/` implements the vendored shaper of phase 3 of `TEXT-RENDERING-PLAN.md`.

## Summary

`HarfbuzzTextEngine` shapes correctly and agrees with the Skia oracle on glyph count, glyph positions
and measured width. But a run it produces carries an **empty `FontHandle`**, so it cannot be drawn. This
is not an omission in the engine; it is a missing capability in the renderer build.

## Defect

1. **The vendored engine reads a font from a file, and the renderer cannot be given one.**
   `SkiaRendererBackend`'s draw path resolves `ShapedRun::Line::font` as an `SkTypeface`. This Skia build
   exposes **no public way to create a typeface from font bytes**:
   - `SkTypeface::MakeFromData` does not exist in this version;
   - `SkFontMgr::RefDefault()` does not exist either;
   - `SkFontMgr_New_Custom_Data` is declared in `skia/ports/SkFontMgr_data.h` but is **not exported**
     by `libskia.a`, so the portable data font manager cannot be linked.

2. **The only exported font manager is the platform one.** `nm` over `libskia.a` shows exactly one:
   `SkFontMgr_New_CoreText`. Using it would make a vendored engine depend on the platform it exists
   to replace, which defeats the purpose of the swap (§3.5, AD-19, AD-28).

## Cause

Phase 3 of the text plan assumes shaping is the only Skia dependency in the text stack. It is not.
**The typeface is the second one**, and it is the harder of the two: shaping is a library that can be
linked directly, while a typeface is a renderer-side object with no portable constructor in this build.

The plan already contains the answer — **phase 4, compile time glyph embedding**, exists precisely to
remove the runtime font dependency — but phase 3 as written underestimates what it can reach on its own.

## What the conformance suite does and does not cover

`tests/text_conformance.test.cpp` compares the two engines on glyph count, every glyph position,
measured width, accented and punctuated text, and reproducibility. That is the transition criterion
phase 3 states: *"glyph positions compared within tolerance"*.

It deliberately does **not** draw. Two tests state the boundary so it cannot be mistaken for finished:
`TheVendoredRunIsNotYetDrawable` asserts `drawable()` is false and the font handle is empty, and
`TheOracleRunIsDrawable` asserts the oracle's handle is valid.

## Plan

1. **Decide where a drawable typeface comes from for a vendored run.** Three shapes:
   - the renderer gains a FreeType or HarfBuzz path so it can build a typeface from the bytes it is
     already handed, which is the honest dependency-free end state and touches AD-28;
   - `FontHandle` grows a second alternative that names a font file rather than a typeface, and the
     renderer resolves it, which keeps the type opaque but pushes resolution into every backend;
   - the platform font manager supplies the typeface by family name, which is the least work and
     abandons the "from a file" property the vendored engine was built for.
2. **Add drawing to the conformance suite once one of those lands**, so a run is compared as pixels and
   not only as numbers.
3. **Only then delete `text/text-skia/`**, which is what phase 3's own transition criterion requires.
   The oracle is still the drawable engine, so deleting it today would leave the framework with an
   engine that measures correctly and draws nothing.

## Verification

- `HarfbuzzTextEngine::ready()` is true with a face, and `create` returns null for a path that names no font.
- Both engines agree on glyph count, on every glyph position within 0.75 logical pixels, and on measured
  width within 1.0, which admits rounding rather than hiding a difference.
- A run from the vendored engine is reproducible across repeated measures.
- A run from the vendored engine reports `drawable() == false` and carries an empty font handle.