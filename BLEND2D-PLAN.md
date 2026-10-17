# Blend2D Render Backend — Implementation Plan

## 1. Objective

Add a third render backend, `render-blend2d`, that implements the Albor rendering HAL
(`RendererBackend`, `RenderSurface`, `RenderContext`) on top of the
[Blend2D](https://blend2d.com) vector engine. The backend is a **CPU rasterizer** — a
sibling of `render-software` — so it is a headless-friendly, deterministic surface that
can be compared against the Skia software reference through the existing `Screenshot` /
`capture_surface` parity path (§6.3, AD-26).

This is a prototype. Its purpose is to prove that Blend2D can cover the full `RenderContext`
contract well enough to be a candidate replacement for Skia. It lives on the
`blend2d-prototype` branch and is not wired into any shipping configuration.

### Non-goals

- **No GPU presentation.** This backend draws into a `BLImage`. Uploading to a `CAMetalLayer`
  is a later step and is out of scope here.
- **No change to the HAL.** The `RenderContext` / `RenderSurface` / `RendererBackend`
  interfaces are untouched. The Blend2D backend is a new implementation behind the same
  contract.

## 2. Capability analysis

The vcpkg port builds the Blend2D core (`libblend2d.a`) with the JIT compiler (`asmjit`).
The installed headers are under `include/blend2d.h` (umbrella) and `include/blend2d/*.h`.

| Albor `RenderContext` op | Blend2D API | Verdict |
| --- | --- | --- |
| `save()` / `restore()` | `BLContext::save()` / `restore()` | ✅ exact |
| `set_transform(Transform)` | `BLContext::setTransform(BLMatrix2D)` | ✅ exact |
| `clip_rect(Rect)` | `BLContext::clipToRect(double,double,double,double)` | ✅ exact |
| `clip_rounded_rect(Rect, r)` | — no rounded clip; only `clipToRect` | ⚠️ approximate (see §7.3) |
| `set_opacity(float)` | (folded into color alpha, mirroring Skia) | ✅ exact |
| `draw_rect(Rect, Paint)` | `fillRect` + `strokeRect` | ✅ exact |
| `draw_rounded_rect(Rect, r, Paint)` | `fillRoundRect` + `strokeRoundRect` | ✅ exact |
| `draw_polyline(points, StrokeStyle)` | `BLPath` + `strokePath` (round cap/join) | ✅ exact |
| `draw_shadow(Rect, r, Shadow)` | no image filter / blur in core | ⚠️ manual blur (§8) |
| `draw_text(TextLayout, origin, Paint)` | `fillGlyphRun(BLFont, BLGlyphRun)` | ✅ supported (§9) |
| `draw_line_batch(LineBatch, T, Stroke)` | `BLPath` + `applyTransform` + `strokePath` | ✅ exact |
| `draw_path(PathResource, T, Paint)` | `BLPath` + `applyTransform` + `fillPath`/`strokePath` | ✅ exact |
| `draw_picture(PictureResource, T)` | `save` + `applyTransform` + replay + `restore` | ✅ exact |
| `draw_image(ImageResource, Rect, Paint)` | `BLImage` + `blitImage(BLRect, src)` | ✅ exact (swizzle, §7.4) |
| `read_pixels()` | `blImageGetData` | ✅ with byte swizzle (§7.2) |

Supporting facts confirmed against the installed headers:

- `BLContext::begin(BLImage&)` / `end()` / `flush()` — frame lifecycle.
- `BLImage(int w, int h, BLFormat)` + `BLImageData { pixelData, stride, size, format, flags }`
  via `blImageGetData`.
- `BL_FORMAT_PRGB32` = premultiplied ARGB (Cairo `ARGB32`), matching Skia's `N32Premul`.
- `BLMatrix2D(m00, m01, m10, m11, m20, m21)`; point mapping is
  `x' = m00*x + m10*y + m20`, `y' = m01*x + m11*y + m21` (same shape as Albor `Transform`).
- `BLContext::setTransform` (assign user matrix), `applyTransform` (concat), `userToMeta()`.
- `setFillStyle` / `setStrokeStyle` (accept `BLRgba32`), `setStrokeWidth(double)`,
  `setStrokeCaps(BL_STROKE_CAP_ROUND)`, `setStrokeJoin(BL_STROKE_JOIN_ROUND)`.
- `fillGlyphRun` / `strokeGlyphRun` / `blFontGetGlyphRunOutlines` are fully implemented in
  the core. Fonts load from raw bytes via `blFontFaceCreateFromData` → `bl::OpenType::createOpenTypeFace`,
  with no external dependency: Blend2D carries its own OpenType engine (`src/blend2d/opentype/`,
  `otglyf.cpp` for TrueType, `otcff.cpp` for PostScript/CFF outlines) plus a SIMD glyph
  rasterizer in `raster/rastercontext.cpp`. `BLGlyphId` is `uint32_t`. No FreeType backend
  exists in this Blend2D version at all — it was removed and replaced by the built-in engine.

## 3. File layout

```
render/render-blend2d/
  CMakeLists.txt                     # new target + -auto WHOLE_ARCHIVE install target
  blend2d_render_context.h           # portable: RenderContext impl, fwd-declares Blend2D types
  blend2d_render_context.cpp
  blend2d_render_surface.h           # portable: RenderSurface impl, pimpl
  blend2d_render_surface.cpp
  blend2d_renderer_backend.h         # portable: RendererBackend impl
  blend2d_renderer_backend.cpp
  blend2d_backend_install.cpp        # load-time auto-install + albor_renderer_marker
```

Naming follows the `render-skia`/`render-software` convention: the library name is the
infix-bearing directory (`render-blend2d`), the `blend2d_` prefix mirrors `skia_`/
`software_`, and every file is portable (`.cpp` / `.h`) because Blend2D is a pure C++ library
that reaches no platform API — exactly the same rule that keeps `skia_render_context.cpp`
portable.

No `.macos.mm` source exists in this module.

## 4. Build integration

### 4.1 `render/render-blend2d/CMakeLists.txt`

```cmake
# the blend2d backend that implements the rendering hal (§3.2, AD-26)

# the config package the port documents, mirroring how skia is discovered once
find_package(blend2d CONFIG REQUIRED)

# gather cpp sources, which reach only blend2d and therefore build on every platform (§3.5, AD-05)
file(GLOB_RECURSE CPP_SOURCES "./*.cpp")

# create the static library from the gathered sources
add_library(albor-render-blend2d STATIC ${CPP_SOURCES})

# the backend implements the rendering hal and names albor types only (§3.5, AD-05)
target_include_directories(albor-render-blend2d PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(albor-render-blend2d PUBLIC cxx_std_23)

# blend2d is consumed as a system dependency, because its warnings are not the framework's (§3.5)
target_link_libraries(albor-render-blend2d PUBLIC albor-render PRIVATE blend2d::blend2d)

# the auto target links the archive whole, so the cpu backend installs itself at load without a call (§6.3, AD-26)
add_library(albor-render-blend2d-auto INTERFACE)
target_link_libraries(albor-render-blend2d-auto INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,albor-render-blend2d>")
```

### 4.2 Root `CMakeLists.txt`

Add a single `add_subdirectory` after the software backend (line ~157):

```cmake
# the blend2d cpu backend that reimplements the rendering hal (§3.2, AD-26)
add_subdirectory(render/render-blend2d)
```

No change to `vcpkg.json` is needed beyond what is already staged: the `blend2d` dependency
with the `jit` feature is present. (If a non-Apple CI runner must also configure, note that
Blend2D has no platform qualifier in the manifest, so it is always built — correct, since it
is portable.)

## 5. The render context

`blend2d_render_context.h` mirrors `skia_render_context.h` exactly, substituting Blend2D
types. The public header carries no Blend2D include — only global-scope forward declarations
(the same trick Skia uses for `SkCanvas`/`SkMatrix`/`SkPath`).

```cpp
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "render/path_resource.h"
#include "render/render_context.h"

// the blend2d context, matrix, path and colour the implementation builds, so that no
// blend2d header reaches this file (§3.5, AD-05)
class BLContext;
class BLPath;
struct BLMatrix2D;
struct BLRgba32;

namespace albor::blend2d
{
    // the render context that wraps one blend2d context for the duration of a frame (§6.3)
    class Blend2DRenderContext final : public RenderContext
    {
    public:
        // wrap the context of the frame being drawn, scaled from logical points onto the physical raster (§6.3, AD-27)
        Blend2DRenderContext(BLContext* context, float device_scale);

        void save() override;
        void restore() override;
        void set_transform(const Transform& transform) override;
        [[nodiscard]] const Transform& transform() const override;
        void clip_rect(const Rect& rect) override;
        void clip_rounded_rect(const Rect& rect, float radius) override;
        [[nodiscard]] const Rect& clip() const override;
        void set_opacity(float opacity) override;
        [[nodiscard]] float opacity() const override;
        void draw_rect(const Rect& rect, const Paint& paint) override;
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;
        void draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) override;
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

        // hand out how deep the state stack is, which a test asserts against the saves it made (§6.2)
        [[nodiscard]] std::size_t depth() const;

    private:
        // the transform and clip one save pushed (§6.2)
        struct State
        {
            Transform transform;
            Rect clip;
            float opacity = 1.0f;
        };

        // convert an albor colour into the blend2d colour the context expects, with the current opacity folded into the alpha (§6.1)
        [[nodiscard]] BLRgba32 to_blend_color(const Color& color) const;

        // fill and stroke a shape, which is the paint handling both rectangles share (§6.2)
        void paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius);

        // convert an albor transform into the blend2d matrix the context assigns, which is the transform without the base scale (§6.3)
        [[nodiscard]] static BLMatrix2D to_blend_matrix(const Transform& transform);

        // build the blend2d path of a command stream, which the batch and the path replay share (§6.4)
        [[nodiscard]] static BLPath build_path(std::span<const PathVerb> verbs, std::span<const Point> points);

        // the context of the frame being drawn, which is null when no frame began (§6.2)
        BLContext* m_context;

        // the scale from logical points onto the physical raster (§6.3, AD-27)
        float m_device_scale;

        // the current transform (§6.2)
        Transform m_transform;

        // the current clip (§6.2)
        Rect m_clip;

        // the opacity the later draws are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> m_states;
    };
} // namespace albor::blend2d
```

### 5.1 Device scale (meta matrix)

The raster is allocated at **physical pixel** size, and every draw happens in **logical**
coordinates. The scale is folded into Blend2D's *meta matrix* once at construction, so all
logical transforms (`set_transform`, retained-draw `applyTransform`) work in logical space,
exactly as the Skia software surface applies `m_canvas->scale(device_scale)` at construction.

```cpp
Blend2DRenderContext::Blend2DRenderContext(BLContext* context, float device_scale) :
    m_context(context), m_device_scale(device_scale) {
    // a context with no target draws nothing, so it starts with an empty clip (§6.2)
    if (m_context != nullptr) {
        // the meta matrix maps logical points onto the physical raster, which every later
        // user transform composes with (§6.3, AD-27)
        m_context->scale(m_device_scale, m_device_scale);
        m_context->userToMeta();
        // the clip bookkeeping stays in logical points, which is the space the scene drew in (§6.3, AD-27)
        m_clip = ...; // full logical extent, passed in or derived from metrics
    }
}
```

The logical clip extent must be known at construction. The Skia surface derives it from the
canvas image info; the Blend2D surface knows the logical size from `metrics()`, so it passes
the full logical `Rect` into the context constructor (see §6). To keep the constructor
signature minimal, the full logical clip is set by the surface immediately after construction
via `clip_rect(full)`, or the constructor takes the `WindowMetrics`. The plan chooses the
simplest correct option: the surface calls `context.set_transform(Transform{})` and
`context.clip_rect(full)` are not needed — instead the context records `m_clip` from a
constructor parameter. Implementation detail, resolved in §6.

### 5.2 Transform mapping

The Albor `Transform` and Blend2D `BLMatrix2D` share the same point mapping, so the
conversion is a direct field reorder:

```cpp
BLMatrix2D Blend2DRenderContext::to_blend_matrix(const Transform& t) {
    // the column order mirrors the albor transform: m00=scale_x, m10=skew_x, m20=translate_x,
    // m01=skew_y, m11=scale_y, m21=translate_y (§6.3)
    return BLMatrix2D(t.scale_x, t.skew_y, t.skew_x, t.scale_y, t.translate_x, t.translate_y);
}
```

`set_transform` assigns the user matrix only (meta holds the device scale):

```cpp
void Blend2DRenderContext::set_transform(const Transform& transform) {
    // a context with no target keeps its own transform bookkeeping only (§6.2)
    if (m_context != nullptr)
        m_context->setTransform(to_blend_matrix(transform));
    m_transform = transform;
}
```

### 5.3 Save / restore / clip

Mirror the Skia implementation, including the manual clip-intersection bookkeeping:

```cpp
void Blend2DRenderContext::save() {
    m_states.push_back(State{m_transform, m_clip, m_opacity});
    if (m_context != nullptr)
        m_context->save();
}

void Blend2DRenderContext::restore() {
    if (m_states.empty())
        return;
    if (m_context != nullptr)
        m_context->restore();
    const State state = m_states.back();
    m_states.pop_back();
    m_transform = state.transform;
    m_clip = state.clip;
    m_opacity = state.opacity;
}

void Blend2DRenderContext::clip_rect(const Rect& rect) {
    // the recorded clip is the intersection of what was there and what was asked for (§6.2)
    const float left = std::max(m_clip.x, rect.x);
    const float top = std::max(m_clip.y, rect.y);
    const float right = std::min(m_clip.x + m_clip.width, rect.x + rect.width);
    const float bottom = std::min(m_clip.y + m_clip.height, rect.y + rect.height);
    m_clip = Rect::from_origin_size(Point{left, top}, Size{std::max(0.0f, right - left), std::max(0.0f, bottom - top)});
    if (m_context != nullptr)
        m_context->clipToRect(rect.x, rect.y, rect.width, rect.height);
}
```

### 5.4 Color

Fold opacity into the color's alpha, exactly like `SkiaRenderContext::to_skia_color`. Blend2D
treats a `BLRgba32` style as straight (non-premultiplied) alpha and premultiplies internally,
so passing straight alpha is correct.

```cpp
BLRgba32 Blend2DRenderContext::to_blend_color(const Color& color) const {
    // with_alpha already scales the colour's own alpha, so passing the opacity alone scales it once (§6.1)
    const Color dimmed = color.with_alpha(m_opacity);
    // the albor colour is in the zero to one range, while blend2d packs one byte per channel (§6.3)
    const auto channel = [](float value) -> std::uint32_t {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(clamped * 255.0f);
    };
    return BLRgba32(channel(dimmed.red), channel(dimmed.green), channel(dimmed.blue), channel(dimmed.alpha));
}
```

### 5.5 Shapes, polylines, paths, batches

```cpp
void Blend2DRenderContext::paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius) {
    if (m_context == nullptr)
        return;
    if (rect.empty())
        return;
    // a rounded shape clamps its radius, because a radius larger than half the extent cannot be drawn (§6.2)
    const double effective_radius = rounded ? std::min(static_cast<double>(radius), std::min(rect.width, rect.height) * 0.5) : 0.0;
    if (paint.fills()) {
        m_context->setFillStyle(to_blend_color(paint.fill));
        if (rounded)
            m_context->fillRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
        else
            m_context->fillRect(rect.x, rect.y, rect.width, rect.height);
    }
    if (paint.strokes()) {
        m_context->setStrokeStyle(to_blend_color(paint.stroke));
        m_context->setStrokeWidth(paint.stroke_width);
        if (rounded)
            m_context->strokeRoundRect(rect.x, rect.y, rect.width, rect.height, effective_radius);
        else
            m_context->strokeRect(rect.x, rect.y, rect.width, rect.height);
    }
}
```

`draw_polyline` builds one `BLPath` and strokes it with round cap/join (identical intent to
the Skia path):

```cpp
void Blend2DRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
    if (m_context == nullptr)
        return;
    if (points.size() < 2 || stroke.width <= 0.0f)
        return;
    BLPath path;
    path.moveTo(points.front().x, points.front().y);
    for (std::size_t index = 1; index < points.size(); index++)
        path.lineTo(points[index].x, points[index].y);
    m_context->setStrokeStyle(to_blend_color(stroke.color));
    m_context->setStrokeWidth(stroke.width);
    m_context->setStrokeCaps(BL_STROKE_CAP_ROUND);
    m_context->setStrokeJoin(BL_STROKE_JOIN_ROUND);
    m_context->strokePath(path);
}
```

`build_path` translates the `PathVerb` stream the same way `SkiaRenderContext::build_path`
does (`MoveTo`/`LineTo`/`Close`). `draw_line_batch` builds a path of two-point sub-contours;
`draw_path` fills/strokes the built contour. Both wrap the retained transform with
`save` + `applyTransform` + draw + `restore`, matching the Skia `concat` behaviour:

```cpp
void Blend2DRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
    if (m_context == nullptr)
        return;
    if (!path || path->verbs().empty())
        return;
    const BLPath contour = build_path(path->verbs(), path->points());
    m_context->save();
    m_context->applyTransform(to_blend_matrix(transform));
    if (paint.fills()) {
        m_context->setFillStyle(to_blend_color(paint.fill));
        m_context->fillPath(contour);
    }
    if (paint.strokes()) {
        m_context->setStrokeStyle(to_blend_color(paint.stroke));
        m_context->setStrokeWidth(paint.stroke_width);
        m_context->strokePath(contour);
    }
    m_context->restore();
}
```

`draw_line_batch` is identical except it builds the path from the batch's segment pairs and
sets `BL_STROKE_CAP_ROUND` / round join (matching the Skia `drawPath` stroke that follows
the batch segments).

### 5.6 Pictures

Pictures replay their own command list under a transform. Blend2D has no display-list type,
but `PictureResource` is already a `std::vector<DrawCommand>`, and the shared
`replay_commands` helper does the work:

```cpp
void Blend2DRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
    if (!picture || picture->empty())
        return;
    m_context->save();
    m_context->applyTransform(to_blend_matrix(transform));
    replay_commands(picture->commands(), *this, m_clip);
    m_context->restore();
}
```

## 6. The render surface

`blend2d_render_surface.h` follows `software_render_surface.h`: a pimpl (`Blend2DSurfaceState`)
keeps every Blend2D type out of the public header.

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "render/render_context.h"
#include "window/window_metrics.h"

namespace albor::blend2d
{
    // the raster and the context of the frame being drawn, so that no blend2d header leaves this file (§3.5)
    struct Blend2DSurfaceState;

    // the surface that rasterizes on the cpu through blend2d (§6.3, AD-26)
    class Blend2DRenderSurface final : public RenderSurface
    {
    public:
        [[nodiscard]] static std::unique_ptr<Blend2DRenderSurface> create(WindowMetrics metrics);
        ~Blend2DRenderSurface() override;
        // (deleted copy/move)

        [[nodiscard]] bool valid() const override;
        [[nodiscard]] const WindowMetrics& metrics() const override;
        void resize(const WindowMetrics& metrics) override;
        RenderContext& begin_frame() override;
        void end_frame() override;
        [[nodiscard]] std::vector<std::uint8_t> read_pixels() const override;
        [[nodiscard]] std::size_t frame_count() const;

    private:
        explicit Blend2DRenderSurface(WindowMetrics metrics);
        std::unique_ptr<Blend2DSurfaceState> m_state;
    };
} // namespace albor::blend2d
```

The state (in the `.cpp`) owns the `BLImage` raster and the `BLContext`:

```cpp
#include <blend2d.h>

namespace albor::blend2d
{
    struct Blend2DSurfaceState
    {
        // the raster frame the context draws into (§6.3, AD-26)
        BLImage raster;

        // the context wrapping the raster (§6.3)
        BLContext context;

        // the context over the raster, which draws logical coordinates onto physical pixels (§6.3)
        std::unique_ptr<Blend2DRenderContext> draw_context;

        // the metrics the surface presents at (§6.1, D9.1)
        WindowMetrics metrics;

        // the frames finished on this surface (§4.3)
        std::size_t frames = 0;

        // whether a frame is currently begun (§6.1)
        bool begun = false;
    };
}
```

Construction and resize allocate a `BLImage` at physical pixel extent:

```cpp
std::unique_ptr<Blend2DRenderSurface> Blend2DRenderSurface::create(WindowMetrics metrics) {
    if (!metrics.valid())
        return nullptr;
    return std::unique_ptr<Blend2DRenderSurface>(new Blend2DRenderSurface(metrics));
}

Blend2DRenderSurface::Blend2DRenderSurface(WindowMetrics metrics) :
    m_state(std::make_unique<Blend2DSurfaceState>()) {
    m_state->metrics = metrics;
    // the raster is physical-pixel sized and premultiplied, matching the skia reference (§6.3, AD-26)
    m_state->raster.create(static_cast<int>(metrics.pixel_size.width),
                           static_cast<int>(metrics.pixel_size.height),
                           BL_FORMAT_PRGB32);
    // a refused allocation leaves the raster empty, so the surface reports unusable (§6.2)
    if (!m_state->raster.empty()) {
        // the context draws logical coordinates scaled onto the physical raster (§6.3, AD-27)
        m_state->draw_context = std::make_unique<Blend2DRenderContext>(&m_state->context, metrics.scale_factor);
        // record the full logical clip from the metrics, which the context needs for bookkeeping (§6.2)
        m_state->draw_context->clip_rect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
    }
}
```

> Note: the exact mechanism for seeding the context's initial logical clip is a small detail —
> it can be a constructor parameter or a post-construction `clip_rect`. The plan records the
> *full logical rect* as the initial clip either way, mirroring Skia's constructor.

`begin_frame` / `end_frame` mirror the software surface. Blend2D's `end()` implicitly flushes
synchronously:

```cpp
RenderContext& Blend2DRenderSurface::begin_frame() {
    if (!valid())
        return *m_state->draw_context; // refuses by drawing into a null-context no-op
    m_state->context.begin(m_state->raster);
    // the raster is cleared to transparent, because a frame paints its own background (§6.1)
    m_state->context.clearAll();
    m_state->begun = true;
    return *m_state->draw_context;
}

void Blend2DRenderSurface::end_frame() {
    if (!valid() || !m_state->begun)
        return;
    m_state->begun = false;
    // blend2d end() flushes synchronously, which is the whole of cpu presentation (§6.3)
    m_state->context.end();
    m_state->frames++;
}
```

### 6.1 `valid()`

A surface is valid when the raster allocated non-empty and a draw context exists:

```cpp
bool Blend2DRenderSurface::valid() const {
    return m_state != nullptr && !m_state->raster.empty() && m_state->draw_context != nullptr;
}
```

## 7. Read-back and pixel formats (parity, §6.3, AD-26)

### 7.1 Canonical byte order

The canonical read is tightly-packed RGBA premultiplied bytes (see
`render/render-software/software_render_surface.cpp` `canonical_info`, and
`render/screenshot.h`). Skia's `kRGBA_8888_SkColorType` produces **R,G,B,A** byte order in
memory.

### 7.2 Blend2D's PRGB32 byte order

Blend2D `BL_FORMAT_PRGB32` is Cairo `ARGB32` premultiplied: a 32-bit value `0xAARRGGBB` in
native endian. On little-endian (x64 macOS/Windows/Linux — the only targets this framework
builds), that value is stored in memory as **B,G,R,A** bytes. To be byte-for-byte comparable
with the Skia reference, `read_pixels` swaps red and blue:

```cpp
std::vector<std::uint8_t> Blend2DRenderSurface::read_pixels() const {
    if (!valid())
        return {};
    if (m_state->frames == 0 || m_state->begun)
        return {};
    BLImageData data;
    if (m_state->raster.getData(&data) != BL_SUCCESS)
        return {};
    const int width = data.size.w;
    const int height = data.size.h;
    if (width <= 0 || height <= 0)
        return {};
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; y++) {
        // the row is walked from the top, honouring the padded stride blend2d reports (§6.3)
        const std::uint8_t* src = static_cast<const std::uint8_t*>(data.pixelData) + static_cast<std::intptr_t>(y) * data.stride;
        std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; x++) {
            // prgb32 is 0xAARRGGBB in memory as B,G,R,A on little endian, so red and blue swap into the canonical R,G,B,A (§6.3, AD-26)
            dst[0] = src[2]; // R
            dst[1] = src[1]; // G
            dst[2] = src[0]; // B
            dst[3] = src[3]; // A
            src += 4;
            dst += 4;
        }
    }
    return pixels;
}
```

> Big-endian is out of scope (no supported target is big-endian). A `static_assert` on
> endianness may be added, but is optional for the prototype.

### 7.3 Rounded clip approximation

Blend2D exposes `clipToRect` but no rounded-rectangle or path clip. `clip_rounded_rect`
degrades to its bounding rectangle — the same reduction `PictureRecorder::clip_rounded_rect`
already performs for the recorded clip:

```cpp
void Blend2DRenderContext::clip_rounded_rect(const Rect& rect, float) {
    // a rounded clip is not expressible in blend2d, so it reduces to its rectangle, which is
    // what the recorder already does for the recorded clip (§6.2)
    clip_rect(rect);
}
```

No widget currently relies on a true rounded clip, so this divergence has no observable
effect today; it is recorded here so a future rounded-clip consumer knows the limitation.

### 7.4 Image upload byte order

`ImageResource` carries RGBA8 bytes in **R,G,B,A** order (premultiplied, per the Skia upload).
Blend2D PRGB32 wants B,G,R,A. `draw_image` therefore swizzle-copies into a scratch `BLImage`
and blits it, scaled to the destination:

```cpp
void Blend2DRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
    if (m_context == nullptr)
        return;
    if (!image || image->pixels().empty() || destination.empty())
        return;
    // the upload is swizzled from the resource's r,g,b,a order into blend2d's prgb32 order (§6.4)
    BLImage upload(static_cast<int>(image->width()), static_cast<int>(image->height()), BL_FORMAT_PRGB32);
    BLImageData data;
    if (upload.makeMutable(&data) != BL_SUCCESS)
        return;
    const std::span<const std::uint8_t> src = image->pixels();
    std::uint8_t* dst = static_cast<std::uint8_t*>(data.pixelData);
    for (std::size_t y = 0; y < image->height(); y++) {
        for (std::size_t x = 0; x < image->width(); x++) {
            const std::size_t s = (y * image->width() + x) * 4;
            const std::size_t d = static_cast<std::size_t>(y) * static_cast<std::size_t>(data.stride) + x * 4;
            dst[d + 0] = src[s + 2]; // B
            dst[d + 1] = src[s + 1]; // G
            dst[d + 2] = src[s + 0]; // R
            dst[d + 3] = src[s + 3]; // A
        }
    }
    // the tint follows the paint, and blend2d scales the image into the destination rectangle (§6.4)
    m_context->setFillStyle(to_blend_color(paint.fills() ? paint.fill : paint.stroke));
    m_context->blitImage(BLRect(destination.x, destination.y, destination.width, destination.height), upload);
}
```

> A retained upload cache keyed on `ImageResource::id()` (the AD-06 "upload once" intent) can
> be added later; the per-frame swizzle is acceptable for the prototype.

## 8. Shadows

Blend2D's core has no image filter and therefore no Gaussian blur, so `draw_shadow` cannot
reuse Skia's `SkImageFilters::Blur`. Two cases:

- **`blur <= 0`**: a hard copy of the caster, offset, filled with the shadow color — exact,
  matching the Skia zero-blur path.
- **`blur > 0`**: render the offset silhouette (round-rect or rect) into a scratch `BLImage`,
  run a separable box blur over the premultiplied channels, then `blitImage` into place.

```cpp
void Blend2DRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
    if (m_context == nullptr)
        return;
    if (!shadow.casts() || rect.empty())
        return;
    const Rect cast = Rect{.x = rect.x + shadow.offset.x, .y = rect.y + shadow.offset.y, .width = rect.width, .height = rect.height};
    // a zero blur is a hard offset copy, which needs no offscreen pass (§6.2)
    if (shadow.blur <= 0.0f) {
        paint_shape(Paint{.fill = shadow.color}, radius > 0.0f, cast, radius);
        return;
    }
    // the reach is three sigmas of the blur, so the offscreen image covers the whole cast (§6.2)
    const float reach = shadow.blur * 3.0f;
    const int w = static_cast<int>(std::ceil(cast.width + reach * 2.0f));
    const int h = static_cast<int>(std::ceil(cast.height + reach * 2.0f));
    BLImage offscreen(w, h, BL_FORMAT_PRGB32);
    {
        BLContext pass;
        pass.begin(offscreen);
        pass.clearAll();
        // the silhouette is the caster moved into the offscreen image (§6.2)
        pass.translate(reach - shadow.offset.x, reach - shadow.offset.y);
        pass.setFillStyle(to_blend_color(shadow.color));
        if (radius > 0.0f)
            pass.fillRoundRect(rect.x, rect.y, rect.width, rect.height, radius);
        else
            pass.fillRect(rect.x, rect.y, rect.width, rect.height);
        pass.end();
    }
    blur_box(offscreen, shadow.blur); // see below
    m_context->blitImage(BLRect(cast.x - reach, cast.y - reach, w, h), offscreen);
}
```

`blur_box` is a separable box blur over the premultiplied PRGB32 buffer (blurring all four
channels, including alpha, which is correct for premultiplied data). It is a private helper in
`blend2d_render_context.cpp` operating on `BLImageData`.

**Divergence note**: a box blur is not a Gaussian. The Skia reference uses a true Gaussian
(`SkImageFilters::Blur`). Shadow edges will differ within the parity tolerance for small blur
radii but may exceed it for large radii. For the prototype this is acceptable; a separable
Gaussian with a width proportional to the blur radius can be substituted if the parity suite
demands closer agreement.

## 9. Text

### 9.1 Correction to the earlier draft

An earlier draft of this plan claimed the vcpkg Blend2D build "ships without its FreeType
font backend" and therefore cannot rasterize text. That claim was **wrong**. Verified against
the actual port and source at the pinned commit:

- Blend2D (2025-03-08) has **no FreeType dependency at all**. The FreeType font backend was
  removed from the project and replaced by a self-contained OpenType engine
  (`src/blend2d/opentype/`: `otglyf.cpp` for TrueType outlines, `otcff.cpp` for
  PostScript/CFF outlines, `otlayout.cpp` for GSUB/GPOS).
- `blFontFaceCreateFromData` builds a face directly from font bytes via
  `bl::OpenType::createOpenTypeFace` — no external backend, no system font lookup.
- The raster context has real (non-null) `fillTextOpD` / `fillGlyphRun` implementations in
  `raster/rastercontext.cpp`, and `blFontGetGlyphRunOutlines` is fully implemented.
- `BLGlyphId` is `uint32_t`.

So the vcpkg port is complete for text. Nothing is missing from `vcpkg.json`.

### 9.2 The real integration challenge

Text *rendering* is available, but the Albor framework does not hand the Blend2D backend a
font it can use. What arrives at `draw_text` is a `TextLayout` carrying a `ShapedRun`:

- glyphs are `ShapedGlyph { uint32_t id, Point position, uint32_t cluster }`, shaped by
  HarfBuzz against a Skia typeface;
- the typeface travels as `FontHandle`, which wraps an opaque `SkTypeface*`.

Blend2D needs a `BLFont` (built from a `BLFontFace` built from **font bytes**, via
`blFontFaceCreateFromData`), and a `BLGlyphRun` of those same glyph IDs. The obstacle is
therefore **not** the rasterizer but the font hand-off: `FontHandle` exposes a `SkTypeface*`,
not the raw font bytes.

The good news: for a TrueType/OpenType face, the glyph IDs Skia reports (SkGlyphID) are the
font's own glyph indices — the very same indices Blend2D's OpenType face expects as
`BLGlyphId`. So if the same font *bytes* are loaded on both sides, the glyph IDs line up
without translation. The work is obtaining those bytes:

1. **Extract the font data from the `SkTypeface`** at the point where the text engine builds
   the `FontHandle` (or where the backend first sees it), cache it keyed by the font, and feed
   it to `blFontFaceCreateFromData`. Skia can hand back the underlying font data; alternatively
   the text engine can retain the original font file bytes it loaded.
2. **Shape with Blend2D** instead of HarfBuzz+Skia for the Blend2D backend, so shaping and
   rasterization share one glyph-ID space by construction.
3. **Glyph-outline text**: tessellate each run into `GlyphOutline` contours on the text-engine
   side (the `text-harfbuzz` engine already does this for embedded fonts) and route general
   text through `draw_path`, keeping rendering entirely within Blend2D.

### 9.3 Prototype behaviour

The prototype lands the *rasterization* path in `draw_text` — build a `BLFont` from the face
and replay the shaped glyph run with `fillGlyphRun` — gated on the font hand-off above. Until
the hand-off is wired, `draw_text` can fall back to the outline path or draw nothing for runs
whose font bytes are unavailable; either way the code path is exercised by a unit test with an
embedded font, where the bytes *are* available:

```cpp
void Blend2DRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
    if (m_context == nullptr || layout.empty())
        return;
    const ShapedRun& run = *layout.run;
    for (const ShapedRun::Line& line : run.lines()) {
        if (line.glyphs.empty() || !line.font.valid())
            continue;
        // resolve the font bytes behind the handle, then build the face and font for this size (§9.2)
        BLFont font = resolve_font(line.font, run.font_size());
        if (font.empty())
            continue;
        // the shaped glyphs become a blend2d glyph run, which rasterizes the same glyph IDs (§9.1)
        BLGlyphRun glyph_run;
        // (fill glyph_run.placementType = ADVANCE_OFFSET, glyphData, placementData)
        m_context->setFillStyle(to_blend_color(paint.fills() ? paint.fill : paint.stroke));
        m_context->fillGlyphRun(BLPoint(origin.x + line.origin.x, origin.y + line.origin.y), font, glyph_run);
    }
}
```

`resolve_font` and the glyph-run packing are the only pieces specific to the font hand-off;
the rest of `draw_text` mirrors the Skia implementation one-to-one. They are the subject of a
follow-up task, documented here so the plan does not overstate the current state.

Embedded-font text is unaffected and renders today: the `GlyphOutline` command kind flows
through `draw_path` (`DrawCommand::replay_glyph_outline`), which the Blend2D backend fully
supports.

## 10. The backend and auto-install

`blend2d_renderer_backend.h` mirrors `software_renderer_backend.h`: a CPU rasterizer that is
always ready and ignores the native window handle.

```cpp
#pragma once

#include <memory>

#include "render/renderer_backend.h"

namespace albor::blend2d
{
    class Blend2DRendererBackend final : public RendererBackend
    {
    public:
        Blend2DRendererBackend();
        ~Blend2DRendererBackend() override;
        // (deleted copy/move)

        [[nodiscard]] bool ready() const override;
        [[nodiscard]] std::unique_ptr<RenderSurface> create_surface(NativeWindowHandle handle, const WindowMetrics& metrics) override;
        void resize_surface(RenderSurface& surface, const WindowMetrics& metrics) override;
        void execute_frame(RenderSurface& surface, const FrameScene& scene) override;
    };

    void install_blend2d_renderer_backend();
} // namespace albor::blend2d
```

`execute_frame` is byte-identical in structure to the software backend's: begin, draw the
full-surface background, replay the shared command list, end:

```cpp
void Blend2DRendererBackend::execute_frame(RenderSurface& surface, const FrameScene& scene) {
    if (!surface.valid())
        return;
    if (surface.metrics() != scene.metrics())
        return;
    RenderContext& context = surface.begin_frame();
    const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, scene.metrics().logical_size);
    context.draw_rect(full, {.fill = scene.background()});
    replay_commands(scene.commands(), context, full);
    surface.end_frame();
}
```

The auto-install translation unit registers the factory at load and declares a distinct
renderer marker:

```cpp
#include "render/render-blend2d/blend2d_renderer_backend.h"

namespace
{
    struct RendererAutoInstall
    {
        RendererAutoInstall() { albor::blend2d::install_blend2d_renderer_backend(); }
    };
    const RendererAutoInstall g_renderer_auto_install;
} // namespace

// the install mark for the renderer, so two backends on one line refuse the binary (§6.3, AD-26)
extern "C" int albor_renderer_marker = 3;
```

The `-auto` interface target (`WHOLE_ARCHIVE`) selects this backend when linked; the marker
`3` distinguishes it from Skia (`1`) and the software reference (`2`).

## 11. Testing

The prototype is validated against the existing suites rather than by new, framework-specific
harnesses:

1. **Parity (AD-26, §16).** Link the Blend2D backend in a test that renders a `FrameScene`
   once through `SoftwareRenderSurface` and once through `Blend2DRenderSurface`, then compare
   the two `Screenshot`s with `Screenshot::max_channel_difference` at the existing tolerance.
   The corpus is the same `FrameScene::from_primitives` + resource draws used by
   `render_skia.test.cpp` / `text_conformance.test.cpp`.
2. **`RenderContext` unit tests.** A test double is not needed; the existing
   `SkiaRenderContext` tests encode the expected `save`/`restore`/`clip`/`opacity` semantics.
   A parallel `Blend2DRenderContext` test asserts `depth()`, `clip()`, `transform()` and
   `opacity()` read-back, and that `read_pixels` returns the canonical R,G,B,A byte order.
3. **Smoke example.** A `hello-window`-style example linked against
   `albor-render-blend2d-auto` (in place of `albor-render-skia-auto`) proves the auto-install
   path and paints a frame end to end.

Per `AGENTS.md`, unit tests are executed by running the test binary directly
(`./.build-debug/tests/albor-tests`), not via `ctest`.

## 12. Risks and open questions

1. **Meta-matrix composition order.** `setTransform` + `userToMeta` must produce the same
   logical→device mapping as Skia's constructor `scale` + per-transform `postScale`. A
   one-rect unit test at a non-unit scale factor (e.g. 2.0) pins this down.
2. **Byte order.** The R/B swizzle assumes little-endian. All supported targets are LE; a
   `static_assert` can make this explicit.
3. **Shadow blur fidelity.** Box blur vs. Skia's Gaussian is the largest visual divergence.
   Confirm the parity suite's tolerance and, if needed, swap in a separable Gaussian.
4. **Text.** Rasterization is fully supported (built-in OpenType engine, no FreeType needed).
   The remaining work is the font hand-off: `FontHandle` carries a `SkTypeface*`, while
   Blend2D needs the font *bytes* to build a `BLFontFace`. Because Skia and Blend2D both use
   the font's own glyph indices, glyph IDs line up once the same bytes are loaded on both
   sides. Resolved by one of §9.2's approaches when text becomes a requirement for this
   backend.

## 13. Summary

### What Blend2D supports (against the HAL)

Fully and cleanly:

- The complete state machine: `save`/`restore`, `set_transform`, `clip_rect`, opacity.
- All shape primitives: filled/stroked rectangles, rounded rectangles, polylines (round
  cap/join), and arbitrary paths (`fillPath`/`strokePath`).
- Retained drawing: line batches and paths (as `BLPath`), pictures (replayed command lists),
  and images (`blitImage` with scaling).
- Text rasterization: `fillGlyphRun` / `strokeGlyphRun` / `blFontGetGlyphRunOutlines` are
  implemented by Blend2D's own OpenType engine — no FreeType dependency.
- A premultiplied ARGB raster (`PRGB32`) whose memory layout matches Skia's `N32Premul`,
  differing only in R/B byte order — a trivial, deterministic swizzle on read-back.

Approximately, with a documented limitation:

- Rounded clips (reduce to bounding rect; no true rounded/path clip in the core).
- Shadow blur (manual separable blur; no built-in image filter).

Integration work required in this prototype:

- Text is rasterizable, but the framework hands the backend a `SkTypeface*` (`FontHandle`),
  while Blend2D needs the font *bytes* to build a `BLFontFace`. The font hand-off (§9.2) is
  the one remaining piece; glyph IDs line up across engines because both use the font's own
  glyph indices.

### How well it fits

Blend2D is a close architectural match for the Albor HAL. Its immediate-mode `BLContext` maps
almost one-to-one onto `RenderContext`, its matrix and color models mirror Albor's
`Transform`/`Color`, its text engine is self-contained (no FreeType), and its premultiplied-ARGB
raster matches the Skia reference's canonical read-back format. The implementation is materially
smaller than the Skia backend because there is no GPU path, no `GrDirectContext`, and no
Objective-C++/Metal layer to manage — every source file is portable C++.

The one genuine capability gap — the absence of a built-in blur for shadows — is narrow and
contained, and text needs only a font-bytes hand-off rather than a new rasterizer. Neither
requires changing the framework. The result is a strong candidate for a Skia replacement, and an
immediately useful second CPU reference for the AD-26 parity suite today.
