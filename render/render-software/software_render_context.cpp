#include "render/render-software/software_render_context.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include <blend2d.h>

#include "render/shaped_run.h"

namespace albor::software
{
    SoftwareRenderContext::SoftwareRenderContext(BLContext* context, float scale_factor, Rect full_clip) :
        m_context(context), m_inner(context, scale_factor, full_clip), m_scale(scale_factor > 0.0f ? scale_factor : 1.0f) {}

    void SoftwareRenderContext::finish() {
        // the rounded clip groups the frame left open are closed before the raster ends (§6.2)
        m_inner.finish();
    }

    void SoftwareRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a context with no raster or no run draws nothing, which the quiet default is (§6.3)
        if (m_context == nullptr || layout.run == nullptr)
            return;
        // the atlas rasterizes by the run's font, so an embedded run needs no platform face (§6.3, AD-34)
        const std::uint64_t font_hash = layout.run->font_hash();
        // the size the whole run was shaped at, which is the logical size the atlas rasterizes for (§9.1)
        const float font_size = layout.run->font_size();
        // the atlas rasterizes at device resolution, so hidpi text is as sharp as the gpu path (§6.3, AD-26)
        const float raster_size = font_size * m_scale;
        // the opacity the context carries dims every glyph, which is the node property reaching the surface (§6.1)
        const float opacity = m_inner.opacity();
        // the fill or the stroke is the ink, which is the same choice the blend2d context makes (§6.3)
        const Color& ink = paint.fills() ? paint.fill : paint.stroke;
        // whether the atlas drew any glyph, which decides whether blend2d is asked to draw the run instead (§6.3)
        bool drew_any = false;
        for (const ShapedRun::Line& line : layout.run->lines()) {
            for (const ShapedGlyph& glyph : line.glyphs) {
                // the glyph's coverage is the atlas rasterization, which is the reference's own rasterizer (§6.3)
                const std::shared_ptr<const GlyphBitmap> bitmap = m_atlas.glyph(font_hash, glyph.id, raster_size);
                // a glyph the atlas does not carry is left for the blend2d fallback below, which keeps the text (§6.3)
                if (bitmap == nullptr || bitmap->coverage.empty())
                    continue;
                // the pen sits at the run origin, the line baseline and the glyph offset (§9.1)
                const float pen_x = origin.x + line.origin.x + glyph.position.x;
                const float pen_y = origin.y + line.origin.y + glyph.position.y;
                // the coverage becomes a premultiplied tinted image, which blend2d composites (§6.3)
                BLImage mask;
                if (mask.create(static_cast<int>(bitmap->width), static_cast<int>(bitmap->height), BL_FORMAT_PRGB32) != BL_SUCCESS)
                    continue;
                BLImageData data;
                if (mask.getData(&data) != BL_SUCCESS)
                    continue;
                // the tint folds the context opacity into the ink, which every glyph shares (§6.1)
                const float alpha_scale = ink.alpha * opacity;
                for (unsigned int y = 0; y < bitmap->height; y++) {
                    // the source coverage row, which the tint multiplies with (§6.3)
                    const std::uint8_t* coverage_row = bitmap->coverage.data() + static_cast<std::size_t>(y) * bitmap->width;
                    auto* row = static_cast<std::uint8_t*>(data.pixelData) + static_cast<std::intptr_t>(y) * data.stride;
                    for (unsigned int x = 0; x < bitmap->width; x++) {
                        // the opacity and coverage become the alpha, which premultiplies the ink (§6.3)
                        const float alpha = static_cast<float>(coverage_row[x]) / 255.0f * alpha_scale;
                        std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4;
                        // prgb32 is blue, green, red and alpha in memory, each premultiplied (§6.3)
                        pixel[0] = static_cast<std::uint8_t>(std::clamp(ink.blue * 255.0f * alpha, 0.0f, 255.0f) + 0.5f);
                        pixel[1] = static_cast<std::uint8_t>(std::clamp(ink.green * 255.0f * alpha, 0.0f, 255.0f) + 0.5f);
                        pixel[2] = static_cast<std::uint8_t>(std::clamp(ink.red * 255.0f * alpha, 0.0f, 255.0f) + 0.5f);
                        pixel[3] = static_cast<std::uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f) + 0.5f);
                    }
                }
                // the tinted mask lands at the pen plus the bearings, in the logical space the context maps (§9.1)
                const BLRect destination(pen_x + static_cast<float>(bitmap->bearing_x) / m_scale, pen_y + static_cast<float>(bitmap->bearing_y) / m_scale, static_cast<double>(bitmap->width) / m_scale, static_cast<double>(bitmap->height) / m_scale);
                // the glyph composites source over the frame, which is the mode text always uses (§6.3)
                m_context->setGlobalAlpha(1.0);
                m_context->setCompOp(BL_COMP_OP_SRC_OVER);
                m_context->blitImage(destination, mask);
                // the atlas drew this glyph, so the run needs no blend2d fallback (§6.3)
                drew_any = true;
            }
        }
        // a run the atlas could not rasterize is drawn by blend2d, so an unregistered font is never dropped (§6.3)
        if (!drew_any)
            m_inner.draw_text(layout, origin, paint);
    }

    void SoftwareRenderContext::save() {
        // the frame state is blend2d's, so the shared context owns the stack (§6.3)
        m_inner.save();
    }

    void SoftwareRenderContext::restore() {
        // the frame state is blend2d's, so the shared context owns the stack (§6.3)
        m_inner.restore();
    }

    void SoftwareRenderContext::set_transform(const Transform& transform) {
        // the transform is shared with the blend2d context, so shapes and glyphs see one (§6.3)
        m_inner.set_transform(transform);
    }

    const Transform& SoftwareRenderContext::transform() const {
        // the transform is the blend2d context's, which is the one every draw uses (§6.3)
        return m_inner.transform();
    }

    void SoftwareRenderContext::clip_rect(const Rect& rect) {
        // the clip is shared with the blend2d context, so a glyph obeys the same clip a shape does (§6.3)
        m_inner.clip_rect(rect);
    }

    void SoftwareRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // the clip is shared with the blend2d context, so a glyph obeys the same rounded clip a shape does (§6.3)
        m_inner.clip_rounded_rect(rect, radius);
    }

    const Rect& SoftwareRenderContext::clip() const {
        // the clip is the blend2d context's, which is the one every draw uses (§6.3)
        return m_inner.clip();
    }

    void SoftwareRenderContext::set_opacity(float opacity) {
        // the opacity is the blend2d context's, so a shape and a glyph are dimmed by one value (§6.3)
        m_inner.set_opacity(opacity);
    }

    float SoftwareRenderContext::opacity() const {
        // the opacity is the blend2d context's, which is the one the glyph tint reads (§6.3)
        return m_inner.opacity();
    }

    void SoftwareRenderContext::push_layer(float opacity) {
        // the layer is the blend2d context's, so its offscreen composite is the reference every backend matches (§6.2)
        m_inner.push_layer(opacity);
    }

    void SoftwareRenderContext::pop_layer() {
        // the layer closes in the blend2d context, which composites the group once (§6.2)
        m_inner.pop_layer();
    }

    void SoftwareRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a rectangle is the blend2d context's, so the two engines fill it identically (§6.3)
        m_inner.draw_rect(rect, paint);
    }

    void SoftwareRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // a rounded rectangle is the blend2d context's, so the two engines fill it identically (§6.3)
        m_inner.draw_rounded_rect(rect, radius, paint);
    }

    void SoftwareRenderContext::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // a polyline is the blend2d context's, so the two engines stroke it identically (§6.3)
        m_inner.draw_polyline(points, stroke);
    }

    void SoftwareRenderContext::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a shadow is the blend2d context's, which is the blur the reference and the gpu backend share (§6.3)
        m_inner.draw_shadow(rect, radius, shadow);
    }

    void SoftwareRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a retained batch is the blend2d context's, so the two engines replay it identically (§6.4)
        m_inner.draw_line_batch(batch, transform, stroke);
    }

    void SoftwareRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a path is the blend2d context's, which is how an embedded glyph outline is filled on the reference (§6.3, AD-34)
        m_inner.draw_path(path, transform, paint);
    }

    void SoftwareRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture is the blend2d context's, so the two engines replay it identically (§6.4)
        m_inner.draw_picture(picture, transform);
    }

    void SoftwareRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // an image is the blend2d context's, so the two engines composite it identically (§6.4)
        m_inner.draw_image(image, destination, paint);
    }
} // namespace albor::software
