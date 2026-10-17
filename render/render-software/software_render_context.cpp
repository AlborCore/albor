#include "render/render-software/software_render_context.h"

#include <cstdint>
#include <memory>

#include <skia/core/SkBitmap.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkImageInfo.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkSamplingOptions.h>

#include "render/shaped_run.h"

namespace albor::software
{
    SoftwareRenderContext::SoftwareRenderContext(SkCanvas* canvas, float scale_factor) :
        m_canvas(canvas), m_inner(canvas, scale_factor), m_scale(scale_factor > 0.0f ? scale_factor : 1.0f) {}

    void SoftwareRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a context with no canvas or no run draws nothing, which is the quiet default (§6.3)
        if (m_canvas == nullptr || layout.run == nullptr)
            return;
        // the atlas rasterizes by the run's font, so an embedded run needs no platform typeface (§6.3, AD-34)
        const std::uint64_t font_hash = layout.run->font_hash();
        // the size the whole run was shaped at, which is the logical size the atlas rasterizes for (§9.1)
        const float font_size = layout.run->font_size();
        // the atlas rasterizes at device resolution, so hidpi text is as sharp as the gpu path (§6.3, AD-26)
        const float raster_size = font_size * m_scale;
        // the opacity the context carries dims every glyph, which is the node property reaching the surface (§6.1)
        const float opacity = m_inner.opacity();
        // whether the atlas drew any glyph, which decides whether skia is asked to draw the run instead (§6.3)
        bool drew_any = false;
        for (const ShapedRun::Line& line : layout.run->lines()) {
            for (const ShapedGlyph& glyph : line.glyphs) {
                // the glyph's coverage is the atlas rasterization, which is the reference's own rasterizer (§6.3)
                const std::shared_ptr<const GlyphBitmap> bitmap = m_atlas.glyph(font_hash, glyph.id, raster_size);
                // a glyph the atlas does not carry is left for the skia fallback below, which keeps the text (§6.3)
                if (bitmap == nullptr || bitmap->coverage.empty())
                    continue;
                // the pen sits at the run origin, the line baseline and the glyph offset (§9.1)
                const float pen_x = origin.x + line.origin.x + glyph.position.x;
                const float pen_y = origin.y + line.origin.y + glyph.position.y;
                // the fill or the stroke is the ink, which is the same choice the skia context makes (§6.3)
                const Color& ink = paint.fills() ? paint.fill : paint.stroke;
                // the coverage is an alpha mask, which skia tints with the ink and composites (§6.3)
                const SkImageInfo info = SkImageInfo::Make(static_cast<int>(bitmap->width), static_cast<int>(bitmap->height), kAlpha_8_SkColorType, kPremul_SkAlphaType);
                SkBitmap mask;
                // a mask that did not install draws nothing, which is a refusal (§6.3)
                if (!mask.installPixels(info, const_cast<std::uint8_t*>(bitmap->coverage.data()), bitmap->width))
                    continue;
                // the mask lifts to an image, which is what the canvas draws (§6.3)
                sk_sp<SkImage> image = SkImages::RasterFromBitmap(mask);
                // an image that did not lift draws nothing, which is a refusal (§6.3)
                if (!image)
                    continue;
                // the paint carries the ink at the context opacity, which skia applies through the mask (§6.1)
                SkPaint glyph_paint;
                glyph_paint.setColor(SkColorSetARGB(static_cast<U8CPU>(ink.alpha * opacity * 255.0f + 0.5f), static_cast<U8CPU>(ink.red * 255.0f + 0.5f), static_cast<U8CPU>(ink.green * 255.0f + 0.5f), static_cast<U8CPU>(ink.blue * 255.0f + 0.5f)));
                // the quad is antialiased, because a glyph edge is not on the pixel grid (§6.3)
                glyph_paint.setAntiAlias(true);
                // the quad lands at the pen plus the bearings, which are device pixels reduced to logical space (§9.1)
                const SkRect dest = SkRect::MakeXYWH(pen_x + static_cast<float>(bitmap->bearing_x) / m_scale, pen_y + static_cast<float>(bitmap->bearing_y) / m_scale, static_cast<float>(bitmap->width) / m_scale, static_cast<float>(bitmap->height) / m_scale);
                // the tinted mask draws at its place, which is the glyph the reference rasterized (§6.3)
                m_canvas->drawImageRect(image, dest, SkSamplingOptions(SkFilterMode::kNearest), &glyph_paint);
                // the atlas drew this glyph, so the run needs no skia fallback (§6.3)
                drew_any = true;
            }
        }
        // a run the atlas could not rasterize is drawn by skia, so an unregistered font is never dropped (§6.3)
        if (!drew_any)
            m_inner.draw_text(layout, origin, paint);
    }

    void SoftwareRenderContext::save() {
        // the frame state is skia's, so the shared context owns the stack (§6.3)
        m_inner.save();
    }

    void SoftwareRenderContext::restore() {
        // the frame state is skia's, so the shared context owns the stack (§6.3)
        m_inner.restore();
    }

    void SoftwareRenderContext::set_transform(const Transform& transform) {
        // the transform is shared with the skia context, so shapes and glyphs see one (§6.3)
        m_inner.set_transform(transform);
    }

    const Transform& SoftwareRenderContext::transform() const {
        // the transform is the skia context's, which is the one every draw uses (§6.3)
        return m_inner.transform();
    }

    void SoftwareRenderContext::clip_rect(const Rect& rect) {
        // the clip is shared with the skia context, so a glyph obeys the same clip a shape does (§6.3)
        m_inner.clip_rect(rect);
    }

    void SoftwareRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // the clip is shared with the skia context, so a glyph obeys the same clip a shape does (§6.3)
        m_inner.clip_rounded_rect(rect, radius);
    }

    const Rect& SoftwareRenderContext::clip() const {
        // the clip is the skia context's, which is the one every draw uses (§6.3)
        return m_inner.clip();
    }

    void SoftwareRenderContext::set_opacity(float opacity) {
        // the opacity is the skia context's, so a shape and a glyph are dimmed by one value (§6.3)
        m_inner.set_opacity(opacity);
    }

    float SoftwareRenderContext::opacity() const {
        // the opacity is the skia context's, which is the one the glyph tint reads (§6.3)
        return m_inner.opacity();
    }

    void SoftwareRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a rectangle is the skia context's, so the two backends fill it identically (§6.3)
        m_inner.draw_rect(rect, paint);
    }

    void SoftwareRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // a rounded rectangle is the skia context's, so the two backends fill it identically (§6.3)
        m_inner.draw_rounded_rect(rect, radius, paint);
    }

    void SoftwareRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // a polyline is the skia context's, so the two backends stroke it identically (§6.3)
        m_inner.draw_polyline(points, stroke);
    }

    void SoftwareRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a shadow is the skia context's, which is the shared blur the reference and the gpu backend use (§6.3)
        m_inner.draw_shadow(rect, radius, shadow);
    }

    void SoftwareRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a retained batch is the skia context's, so the two backends replay it identically (§6.4)
        m_inner.draw_line_batch(batch, transform, stroke);
    }

    void SoftwareRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a path is the skia context's, which is how an embedded glyph outline is filled on the reference (§6.3, AD-34)
        m_inner.draw_path(path, transform, paint);
    }

    void SoftwareRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture is the skia context's, so the two backends replay it identically (§6.4)
        m_inner.draw_picture(picture, transform);
    }

    void SoftwareRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // an image is the skia context's, so the two backends composite it identically (§6.4)
        m_inner.draw_image(image, destination, paint);
    }
} // namespace albor::software
