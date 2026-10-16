# Blend2D Render Backend — Code Review

Branch: `blend2d-prototype`
Worktree: `/Users/rogelio/Projects/albor-blend2d-prototype`
Review date: 2026-10-16

This review covers every file changed or added on the branch, measured against the architecture
specification (`albor-design-v4.md`), `AGENTS.md`, `STYLE-GUIDE.md`, and — most importantly — the
existing `render-skia` / `render-software` backends, which are the reference behaviour the new
backend must reproduce byte-for-byte within tolerance (AD-26).

---

## 1. Scope of the change

The branch adds a third render backend, `render/render-blend2d`, a CPU rasterizer built on the
[Blend2D](https://blend2d.com) vector engine. It is a sibling of `render-software` and is intended
as a candidate Skia replacement plus a second CPU reference for the AD-26 parity suite. The plan is
written up in `BLEND2D-PLAN.md`.

Files touched:

| File | Change |
| --- | --- |
| `CMakeLists.txt` | adds `add_subdirectory(render/render-blend2d)` |
| `examples/CMakeLists.txt` | adds `add_subdirectory(hello-blend2d)` |
| `examples/hello-window/CMakeLists.txt` | adds `albor-text-skia-auto` to the link line (unrelated) |
| `render/CMakeLists.txt` | excludes `render-blend2d` from the shared `albor-render` glob |
| `tests/CMakeLists.txt` | links `albor-render-blend2d` into `albor-tests` |
| `vcpkg.json` | adds `blend2d` (`2025-03-08#1`, `jit` feature), no platform qualifier |
| `BLEND2D-PLAN.md` | new, implementation plan |
| `render/render-blend2d/*` | new, 12 files |
| `examples/hello-blend2d/*` | new, CMakeLists + `main.cpp` |
| `tests/blend2d.test.cpp` | new, 15 tests |

The work is **uncommitted** (all files show as modified/untracked in `git status`); there is no
commit to review on the branch itself.

---

## 2. Summary of findings

| # | Severity | Area | Finding |
| --- | --- | --- | --- |
| 1 | **Bug** | `draw_image` | Semi-transparent images are double-premultiplied |
| 2 | **Bug** | `draw_image` | `paint` (alpha/tint) is ignored entirely |
| 3 | **Gap** | `draw_text` | Non-embedded text is silently dropped |
| 4 | **Risk** | `draw_text` | Glyph-ID compatibility Skia↔Blend2D is asserted, never verified |
| 5 | **Bug/risk** | font cache | `font_face_cache()` is unsynchronized and never cleared |
| 6 | **Risk** | `read_pixels` | Byte-swizzle assumes little-endian with no guard |
| 7 | **Gap** | retained draws | `applyTransform` ordering is untested (plan risk #1 unresolved) |
| 8 | **Gap** | images | No parity test for scaled or translucent images |
| 9 | **Gap** | text | No parity test for text at all |
| 10 | **Gap** | `line_batch`/`picture` | No parity test exercises either |
| 11 | **Doc drift** | plan | Plan claims "no GPU presentation" / "no `.macos.mm`", implementation added both |
| 12 | **Scope** | `hello-window` | Unrelated text-engine link fix mixed into the branch |
| 13 | **Minor** | build | `blend2d` becomes an unconditional dependency on every platform |

---

## 3. Bug-by-bug analysis

### 3.1 `draw_image` double-premultiplies translucent pixels (BUG, high)

`blend2d_render_context.cpp:500-542`, specifically the upload loop at `:529-538`:

```cpp
const std::uint32_t alpha = source[3];
target[0] = static_cast<std::uint8_t>(source[2] * alpha / 255); // B
target[1] = static_cast<std::uint8_t>(source[1] * alpha / 255); // G
target[2] = static_cast<std::uint8_t>(source[0] * alpha / 255); // R
target[3] = source[3];                                          // A
```

The reference (`render/render-skia/skia_render_context.cpp:425`) uploads `ImageResource` bytes with:

```cpp
SkImageInfo::Make(..., kRGBA_8888_SkColorType, kPremul_SkAlphaType)
```

i.e. the bytes are **already premultiplied**. `ResourceFactory::create_image`
(`render/resource_factory.cpp:26`) copies pixels verbatim and does no conversion, so the de-facto
contract established by the reference backend is *premultiplied RGBA8*. The Blend2D upload
re-multiplies colour by alpha, producing a **double premultiply** for every pixel with
`alpha < 255` (e.g. a pixel `R=255, A=128`, premultiplied to `R=128`, is darkened to `64`).

It is not caught by the only image parity test (`tests/blend2d.test.cpp:80-82`), which uses a fully
opaque `0xff` buffer where premultiply is a no-op.

Either the Blend2D upload must not premultiply (treat input as premultiplied), or the reference and
the Blend2D backend must agree on a single, documented alpha convention. As written they disagree.

### 3.2 `draw_image` ignores the `paint` (BUG, medium)

`blend2d_render_context.cpp:510-511`:

```cpp
// the skia reference tints the upload but blend2d blits untinted, which is the common opaque case (§6.4)
(void)paint;
```

The Skia reference (`skia_render_context.cpp:436-442`) sets the paint colour from `paint.fill` /
`paint.stroke` and draws with it. For a colour (RGBA) image, Skia applies the paint **alpha** as a
modulation; for an alpha-only image it applies the colour as a fill. The Blend2D backend drops both.

Consequence: an image recorded under a node opacity — which `DrawCommand::scale_alpha`
(`render/draw_command.cpp:194`) folds into `paint.fill.alpha` / `paint.stroke.alpha` — renders fully
opaque in Blend2D and correctly dimmed in Skia. This is a real divergence for any translucent image
or tint, hidden by the fact that the parity test uses `Paint{}` on an opaque image (alpha 1.0 is a
no-op).

### 3.3 `draw_text` silently drops non-embedded fonts (GAP, high)

`blend2d_render_context.cpp:398-427` (`resolve_font`) and `:289-293`:

```cpp
const resources::EmbeddedFont* embedded = resources::FontRegistry::find_hash(font_hash);
if (embedded == nullptr)
    return BLFont();
...
if (font.empty())
    continue;  // line skipped
```

The framework's general text path (`DrawCommandKind::Text`) is shaped by HarfBuzz against a Skia
typeface; the typeface travels as `FontHandle` (an opaque `SkTypeface*`). Blend2D needs the font
*bytes* to build a `BLFontFace`. `resolve_font` only resolves fonts registered in
`FontRegistry` by content hash; anything else (system fonts, fallback faces) returns an empty font
and the whole line is **silently skipped**.

This is acknowledged in the plan (§9.2 "The real integration challenge") as the one remaining piece,
but it means the backend is not yet a drop-in for text. There is **no parity test for text** to prove
even the embedded-font (`GlyphOutline` → `draw_path`) path, which the plan claims "renders today".

### 3.4 Glyph-ID compatibility is asserted, not verified (RISK, medium)

`draw_text` packs the shaped `ShapedGlyph::id` straight into `BLGlyphRun.glyphData`
(`blend2d_render_context.cpp:295-316`). The plan (§9.2) claims that Skia and Blend2D use the font's
own glyph indices for TrueType/OpenType so the IDs "line up". This is generally true for TrueType,
but Skia can remap glyph IDs for some face types (PostScript/CFF, fonts with a custom glyph-ID
mapping). There is no unit or parity test with a real embedded font that proves the two engines
produce the same raster for the same shaped run.

### 3.5 `font_face_cache()` is unsynchronized and never invalidated (BUG/risk, medium)

`blend2d_render_context.cpp:26-30`:

```cpp
std::unordered_map<std::uint64_t, BLFontFace>& font_face_cache() {
    static std::unordered_map<std::uint64_t, BLFontFace> cache;
    return cache;
}
```

- **Data race.** `resolve_font` runs on the render path. If the framework ever executes frames on
  more than one render thread (the spec allows render execution on a render thread, §10.2/§10.5),
  `emplace`/`find` on this process-wide static map is a race.
- **Stale entries.** `FontRegistry::clear()` (`resources/embedded_font.cpp:82`) is used by tests to
  isolate registrations, but the face cache is keyed by hash and never cleared, so a face built from
  a font registered in a previous test survives. (Likely harmless today because content hashes are
  stable, but it is an unmanaged global coupling.)

### 3.6 `read_pixels` byte-swizzle has no endianness guard (RISK, low)

`blend2d_render_surface.cpp:142-147` swaps `source[0]`↔`source[2]`, relying on `PRGB32`
(`0xAARRGGBB`) being stored B,G,R,A in little-endian memory. The plan itself flags this ("a
`static_assert` can make this explicit"). All current targets are little-endian, but a big-endian
build would silently produce wrong pixels rather than fail.

---

## 4. Gaps in test coverage

The parity suite (`tests/blend2d.test.cpp`) is good for the aligned-rectangle and soft-shadow cases,
but leaves several contract corners uncovered:

1. **Retained draws with non-identity transforms.** `draw_path`, `draw_line_batch`, `draw_picture`
   all use `BLContext::applyTransform` (`blend2d_render_context.cpp:445, 466, 491`). The only
   retained-draw parity test uses `Transform{}` (identity) for the path and does not exercise the
   batch or picture at all (`blend2d.test.cpp:74-78`). The matrix *composition order*
   (`applyTransform` = `user = user * m` vs Skia `concat` = `M = M * m`) is plan risk #1 and is
   **not pinned** by any test. `AnAlignedRectangleSceneMatchesTheReferenceAtDoubleScale` exercises
   `set_transform` + the meta matrix, but not the concat path.
2. **Image scaling and filtering.** The image test is 8×8 → 8×8 (1:1). `blitImage(BLRect, src)` maps
   to `blitScaledImageD` (scaling), and Blend2D has no exposed image-filter-mode control, while Skia
   uses `SkFilterMode::kLinear`. A non-1:1 destination is untested and may diverge on filtering.
3. **Translucent images / translucent fills.** No test exercises partial alpha on the image path
   (hides bug 3.1 and 3.2).
4. **Text.** No `Text`-command or `GlyphOutline` parity test at all (hides bug 3.3).
5. **Rounded shadow caster.** Both shadow tests use `radius = 0.0f`; the rounded-caster branch of
   `draw_shadow` (`blend2d_render_context.cpp:257-260`) is untested.
6. **Opacity affecting pixel output.** The opacity tests assert `opacity()` read-back only; no
   end-to-end pixel check verifies that `set_opacity` dims a draw identically to the reference.
7. **The `-auto` install path.** Tests construct `Blend2DRendererBackend` directly; nothing asserts
   that linking `albor-render-blend2d-auto` installs the factory and that the `albor_renderer_marker`
   (value `3`) rejects a second backend. Only the `hello-blend2d` example exercises this, manually.

---

## 5. Documentation / plan drift

- **Non-goal contradicted.** `BLEND2D-PLAN.md:18-22` states "No GPU presentation … Uploading to a
  `CAMetalLayer` is a later step" and §3 declares "No `.macos.mm` source exists in this module."
  The implementation adds `blend2d_window_surface.macos.mm`, a full Metal-present window surface
  (`present()` copies the CPU raster through a blit encoder into the drawable). The plan is stale.
- **File layout.** The plan §3 lists 6 files; the module contains 12 (`blend2d_window_surface.{h,cpp,macos.mm}`
  and the `CMakeLists.txt` are unlisted).
- **`draw_image` divergence is mislabelled.** The in-code comment frames the ignored paint as "the
  common opaque case", which understates a real correctness gap (see 3.2).

---

## 6. Build / integration notes

- **Unconditional dependency.** `vcpkg.json` adds `blend2d` with no `platform` qualifier, and
  `render/render-blend2d/CMakeLists.txt` calls `find_package(blend2d CONFIG REQUIRED)`. Every
  platform (Linux/Windows CI) must now build Blend2D. This is acceptable for a prototype but should
  be flagged: the branch makes the backend's dependency cost universal rather than opt-in.
- **Apple-only smoke example.** `examples/hello-blend2d/CMakeLists.txt` returns early on non-Apple,
  even though the CPU backend itself is portable. There is no headless/portable smoke example for
  Linux/Windows or CI.
- **Marker scheme consistent.** `albor_renderer_marker` values are `1` (skia), `2` (software),
  `3` (blend2d). Verified: the test binary (direct link, non-`-auto`) contains **no** marker symbol,
  while `hello-blend2d` and `hello-window` each contain exactly one, so the duplicate-marker link
  guard works as designed.
- **Unrelated change.** `examples/hello-window/CMakeLists.txt` adds `albor-text-skia-auto` (a text
  engine fix) in the same diff as the Blend2D work. This should be split out of the branch.

---

## 7. Things that are correct (verified against the reference)

For completeness, the following were checked against the Skia/software reference and the installed
Blend2D headers and are correct:

- `to_blend_matrix` field mapping matches `Transform::apply` (`core/geometry.cpp:40-47`).
  `BLMatrix2D(m00,m01,m10,m11,m20,m21)` with `x' = x*m00 + y*m10 + m20` agrees with
  `scale_x·x + skew_x·y + translate_x`.
- Meta-matrix construction (`scale` + `userToMeta`) reproduces Skia's `setMatrix(transform.postScale(device_scale))`;
  the `set_transform`/meta composition is right.
- `blitImage(BLRect, src)` maps to `blitScaledImageD`, so `draw_image`'s scaling assumption is API-correct.
- `BLGlyphRun` field layout (`glyphAdvance`/`placementAdvance` are `int8_t`; `glyphData`/`placementData`
  are `void*`) and `BL_GLYPH_PLACEMENT_TYPE_USER_UNITS` ("offset in user-space units", added to the
  origin) match the usage.
- `clearAll()` does not reset the transform, so the meta scale set in the constructor survives.
- `read_pixels` honours the padded stride and swaps to canonical R,G,B,A correctly for LE PRGB32.
- `blend2d_window_surface.macos.mm` sets `framebufferOnly = NO` before blitting into the drawable,
  which the blit-encoder path requires (the Skia path uses `YES` but renders through Skia's GPU
  surface, not a CPU blit).
- `Blend2DRendererBackend::execute_frame` is byte-identical in structure to the software backend
  (metrics check → begin → background fill → `replay_commands` → end).

---

## 8. Recommended follow-up (ordered)

1. Fix the image upload: adopt one alpha convention (recommend premultiplied, matching the
   reference) and stop re-premultiplying; implement the paint alpha modulation (bug 3.1, 3.2).
2. Add parity tests for: a non-identity retained transform on `draw_path`/`draw_line_batch`/`draw_picture`
   (pins plan risk #1); a scaled (non-1:1) image; a translucent image; a rounded-caster shadow.
3. Add an embedded-font `GlyphOutline` parity test to prove the claimed text path; decide and
   document the `Text`-command behaviour (drop vs outline fallback) until the font hand-off lands.
4. Make `font_face_cache()` safe (per-thread or mutex) and reconcile its lifetime with
   `FontRegistry::clear()`.
5. Update `BLEND2D-PLAN.md` to match the implemented window-surface and file layout, or delete it
   if it is no longer the source of truth.
6. Split the `hello-window` text-engine fix out of the branch.

---

## 9. Compliance checklist (AGENTS.md / STYLE-GUIDE)

- File naming: portable sources use `.cpp`/`.h`; the Metal window surface uses `.macos.mm`. ✅
- No Blend2D or platform types in public headers: the surface/context/backend headers forward-declare
  `BLContext`/`BLPath`/`BLFont`/`BLMatrix2D`/`BLRgba32` and keep `BLImage`/`BLContext` behind a pimpl. ✅
- No `#ifdef` configuration splits: the backend is a separate linked target installed via a
  registration hook, matching AD-04. ✅
- Comment style matches the surrounding modules (one comment line above each statement). ✅
- `render/CMakeLists.txt` correctly excludes `render-blend2d` from the shared `albor-render` glob. ✅

One style nit: the module's public headers forward-declare Blend2D types in the **global namespace**
(the same trick Skia uses for `SkCanvas`), which is consistent with the existing pattern but is a
fragile convention worth a comment in the header if it is to be relied on.
