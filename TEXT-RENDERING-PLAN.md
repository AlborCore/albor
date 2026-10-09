# Text Rendering Plan: Shaped Runs, Atlas, and Decoupling from the Render Stack

Plan for the text pipeline. The defects it clears are D1 (text measured shaped, drawn
unshaped) and the text cost of D6, both filed in `RENDERING-DEFECTS.md`; the decision
forks below are open until implementation time.

## Problem

The text engine shapes a run through skparagraph on every measure call — wrapping,
bidi, line metrics (§9.1) — and the draw path throws the shaped result away:

- `TextLayout` is a UTF-8 byte string plus scalar metrics (`render/frame_scene.h:20-42`),
  so the scene carries no glyphs at all.
- `SkiaRenderContext::draw_text` rasterizes the raw bytes with `drawSimpleText` and a
  fabricated font: `SkFont(nullptr, layout.size.height)` uses the run's total height as
  the point size, on the process default typeface
  (`render/render-skia/skia_render_context.cpp:130-137`). Every line of a wrapped run
  lands on one baseline.
- Every measure constructs a fresh shaping paragraph with no cache
  (`text/text-skia/skia_text_engine.cpp:55-97`), so a visual only invalidation re-shapes
  every run in the tree at every frame it triggers.

Measurement and drawing are two independent code paths over different data, which is
why the defect cannot be fixed inside the current run type: carrying the shaped
paragraph object in `TextLayout` would leak a skia type into a core header.

## Target end state

`TextEngine` stays the only contract widgets see (`text/text_engine.h` is already
correct). Its implementation becomes: font source, shaping, rasterization, atlas
packing and caching, producing shaped glyph runs. Each renderer maps the glyphs to
its own glyph cache on demand and draws them; it never knows more about text than the
runs it is handed.

The invariant that keeps the boundary honest, to be written into
`albor-design-v4.md` before any code moves: **no text type names a renderer type, and
no renderer type names a text engine type** (§3.5, AD-19).

## The stack

| role | recommendation | size |
| --- | --- | --- |
| font source | embedded fonts first, CoreText direct next, per platform | small / medium |
| shaping | HarfBuzz, vendored; reads the font blob directly, no ICU needed initially (`hb_unicode_funcs` covers general category and bidi class) | medium |
| cache | shaped run LRU keyed by text, font, size, maximum width | small |
| output | `ShapedRun`: glyph identifiers, glyph positions, a typeface reference, and one baseline origin per line; crosses as `std::shared_ptr<const>` | the core change |
| glyph rasterization | per renderer: the skia backend rasterizes through its own internal glyph cache; the software renderer rasterizes with vendored stb_truetype into its own atlas | small, header only |

Shaping and rasterization are separate concerns at different temperatures: shaping is
expensive and shared across every renderer, rasterization is cheap and owned by each
renderer. `ShapedRun` therefore carries no rasterization output at all — each renderer
maps glyph identifiers to its own glyph cache on demand.

## Phases

### Phase 1: the shaped run type

`TextLayout` becomes a `std::shared_ptr<const ShapedRun>` plus the scalar metrics; the
UTF-8 string as the draw payload goes away. `ShapedRun` is defined in core, names no
renderer type, and carries: glyph identifiers, glyph positions, a typeface reference,
and one baseline origin per line for wrapped runs. This fixes D1 by construction —
stage two replays exactly what stage one measured — and it is also the decoupling
step, because it defines the seam every later phase plugs into.

### Phase 2: the shaped run cache

A keyed lookup in the engine replaces paragraph construction per measure, keyed by
text, font, size and maximum width. This clears the per frame reshape cost (the text
half of D6). It lands after Phase 1 because the cached value is the shaped run;
building the cache first would mean building it twice.

### Phase 3: the vendored swap

Behind the existing `TextEngine` factory, the vendored engine becomes the default.
`text/text-skia/` stays temporarily as a shaping oracle: the dual engine conformance
corpus (§15) runs through both engines, glyph positions compared within tolerance.
When conformance passes, the skia text backend is deleted.

### Phase 4: compile time glyph embedding

A compiler pass rasterizes exactly the glyphs an application needs, at the pixel sizes
and weights its `.ui` declares, into static font data carried in the application
binary, and emits the registration calls in the generated code (§11.3). The runtime
then needs no font discovery, no shaping and no rasterizer dependency for embedded
text. This is the strongest form of the dependency free goal and it belongs to the
compiler, so it is built as part of the phase 4 codegen work; the cache key includes a
quarter-pixel subpixel bin so the pre-rasterized glyphs stay sharp at fractional
origins.

### Phase 5: renderer neutral draw

`RenderContext::draw_text` consumes the shaped run; each renderer maps glyph
identifiers to its own glyph cache: the skia backend rasterizes through its internal
glyph cache and draws the positioned glyphs, the software renderer rasterizes with
vendored stb_truetype into its own atlas and draws textured quads. Text as quads works
with the current imperative `RenderContext` and with the ordered command list of
`RENDERING-DEFECTS.md` alike, so this phase does not block on the scene rework; the
only interlock is keeping the `draw_text` signature coordinated with the command's
text payload.

### Phase 6: renderer becomes a free variable

Once text no longer forces skia, the rendering backend is chosen on measurement alone.
The prototype comparison between wgpu and skia is re-run on the real workload, which
is now glyph quad dominated, and `text-skia/` is removed after conformance.

## Decision forks, open for implementation time

1. **Complex scripts, now or later.** If Arabic, Indic or other complex scripts are
   not imminent, ship the stb only engine first (Latin, zero dependency, immediately)
   and add HarfBuzz when needed; the factory lets both coexist. If i18n is near term,
   start Phase 3 with HarfBuzz from the start. The `i18n/` module in the layout says
   eventually; the sequencing is a product call.
2. **Embedded fonts versus system fonts for the first milestone.** Embedded fonts
   unblock everything with no platform work; CoreText direct gives platform matching
   typography but is per platform work. Embedded first, CoreText as the second
   milestone.
3. **Per renderer glyph cache shape.** Atlas density, page eviction and subpixel
   quantization are renderer-owned decisions once rasterization is per renderer: the
   skia renderer delegates to skia's internal glyph cache, the software renderer owns
   its atlas and decides how pages are baked for scale factors. Decided per renderer
   when the renderer lands, not centrally.

## Guardrails

- Do not build the atlas path while keeping skparagraph as the working path; that pays
  both stacks' costs at once.
- Do not let a skia type touch `ShapedRun`.
- Wrapping must survive the rework: per line origins are part of the run type from the
  first commit, not an afterthought. That is the piece the current `TextLayout`
  entirely lacks.
- Missing glyph fallback is part of the run type semantics, decided with the atlas,
  not bolted on after.

## Verification

- A wrapped two line run draws with the shaped glyphs, at the measured size, with
  correct line breaks.
- The dual engine conformance corpus agrees between the vendored engine and the skia
  oracle within tolerance (§15), after which the oracle is removed.
- A steady state frame performs no shaping work: repeated measure of unchanged text is
  a cache lookup.
- No skia include reaches `text/` outside `text-skia/`, asserted in review and by the
  build scope rules.
- `bash build/check-format.sh` passes on every changed file.
