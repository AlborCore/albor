#include "render/render-skia/skia_render_context.h"

#include <algorithm>

#include <skia/core/SkCanvas.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkRRect.h>
#include <skia/core/SkRect.h>

namespace albor::skia
{
    SkiaRenderContext::SkiaRenderContext(SkCanvas* canvas) :
        m_canvas(canvas) {
        // a context with no canvas draws nothing, so it starts with an empty clip (§6.2)
        if (m_canvas != nullptr) {
            const SkImageInfo& info = m_canvas->imageInfo();
            m_clip = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{static_cast<float>(info.width()), static_cast<float>(info.height())});
        }
    }

    void SkiaRenderContext::save() {
        // the albor state is pushed first, because the canvas save is what the pop restores (§6.2)
        m_states.push_back(State{m_transform, m_clip});
        // a context with no canvas keeps its own stack only (§6.2)
        if (m_canvas == nullptr)
            return;
        m_canvas->save();
    }

    void SkiaRenderContext::restore() {
        // a restore without a matching save is a programming error, so the stack is left alone (§6.2)
        if (m_states.empty())
            return;
        // the canvas is restored before the albor state, because the canvas holds the real clip stack (§6.2)
        if (m_canvas != nullptr)
            m_canvas->restore();
        // the saved state becomes the current one again (§6.2)
        const State state = m_states.back();
        m_states.pop_back();
        m_transform = state.transform;
        m_clip = state.clip;
    }

    void SkiaRenderContext::set_transform(const Transform& transform) {
        // a context with no canvas keeps its own transform bookkeeping only (§6.2)
        if (m_canvas == nullptr) {
            // store transform
            m_transform = transform;
            // exit
            return;
        }
        // the skia matrix is rebuilt from the albor transform, so a save and a restore round trip (§6.2)
        SkMatrix matrix;
        matrix.setScale(transform.scale_x, transform.scale_y);
        matrix.setSkewX(transform.skew_x);
        matrix.setSkewY(transform.skew_y);
        matrix.postTranslate(transform.translate_x, transform.translate_y);
        m_canvas->setMatrix(matrix);
        m_transform = transform;
    }

    const Transform& SkiaRenderContext::transform() const {
        // the recorded transform is what a test asserts against (§6.2)
        return m_transform;
    }

    void SkiaRenderContext::clip_rect(const Rect& rect) {
        // the recorded clip is the intersection of what was there and what was asked for (§6.2)
        const float left = std::max(m_clip.x, rect.x);
        const float top = std::max(m_clip.y, rect.y);
        const float right = std::min(m_clip.x + m_clip.width, rect.x + rect.width);
        const float bottom = std::min(m_clip.y + m_clip.height, rect.y + rect.height);
        m_clip = Rect::from_origin_size(Point{left, top}, Size{std::max(0.0f, right - left), std::max(0.0f, bottom - top)});
        // a context with no canvas keeps its clip bookkeeping only (§6.2)
        if (m_canvas == nullptr)
            return;
        m_canvas->clipRect(SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height), SkClipOp::kIntersect);
    }

    void SkiaRenderContext::clip_rounded_rect(const Rect& rect, float radius) {
        // a rounded clip is a rounded rectangle clip in skia, which expresses the corner radii directly (§6.2)
        if (m_canvas != nullptr) {
            const SkRect shape = SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height);
            m_canvas->clipRRect(SkRRect::MakeRectXY(shape, radius, radius), SkClipOp::kIntersect, true);
        }
        // a rounded clip contains the rectangle it was built from, so the recorded clip is that rectangle (§6.2)
        clip_rect(rect);
    }

    const Rect& SkiaRenderContext::clip() const {
        // the recorded clip is what a test asserts against (§6.2)
        return m_clip;
    }

    void SkiaRenderContext::draw_rect(const Rect& rect, const Paint& paint) {
        // a plain and a rounded rectangle differ only in the shape they build (§6.2)
        paint_shape(paint, false, rect, 0.0f);
    }

    void SkiaRenderContext::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the corner radii are what distinguishes this shape from a plain rectangle (§6.2)
        paint_shape(paint, true, rect, radius);
    }

    void SkiaRenderContext::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a context with no canvas draws nothing (§6.2)
        if (m_canvas == nullptr)
            return;
        // an empty run has no glyphs to draw, so it costs nothing (§6.2)
        if (layout.empty())
            return;
        // the font size comes from the run height, which the ui thread measured (§9.1)
        SkFont font(nullptr, layout.size.height);
        font.setSubpixel(true);
        // the text takes the fill colour when the paint fills, and the stroke colour otherwise (§6.2)
        SkPaint text_paint;
        text_paint.setColor(SkColor(to_skia_color(paint.fills() ? paint.fill : paint.stroke)));
        text_paint.setAntiAlias(true);
        // the origin is the baseline, which is what the run ascent and descent describe (§9.1)
        m_canvas->drawSimpleText(layout.text.c_str(), layout.text.size(), SkTextEncoding::kUTF8, origin.x, origin.y, font, text_paint);
    }

    std::size_t SkiaRenderContext::depth() const {
        // the depth is what a test asserts the saves and restores produced (§6.2)
        return m_states.size();
    }

    unsigned int SkiaRenderContext::to_skia_color(const Color& color) {
        // the albor colour is in the zero to one range, while skia packs one byte per channel (§6.3)
        const auto channel = [](float value) -> unsigned int {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<unsigned int>(clamped * 255.0f);
        };
        // the alpha occupies the high byte, which is the layout skia expects (§6.3)
        return (channel(color.alpha) << 24) | (channel(color.red) << 16) | (channel(color.green) << 8) | channel(color.blue);
    }

    void SkiaRenderContext::paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius) {
        // a context with no canvas draws nothing (§6.2)
        if (m_canvas == nullptr)
            return;
        // a shape with no area contributes nothing, so it is skipped rather than drawn degenerate (§6.2)
        if (rect.empty())
            return;
        // the shape is built in the logical space the context transform maps into (§6.2)
        const SkRect shape = SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height);
        // a rounded shape clamps its radius, because a radius larger than half the extent cannot be drawn (§6.2)
        const SkScalar effective_radius = rounded ? std::min(radius, std::min(rect.width, rect.height) * 0.5f) : 0.0f;
        // a transparent fill draws nothing, which is how a paint carries only a stroke (§6.2)
        if (paint.fills()) {
            SkPaint fill_paint;
            fill_paint.setColor(SkColor(to_skia_color(paint.fill)));
            fill_paint.setAntiAlias(true);
            // the fill of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_canvas->drawRRect(SkRRect::MakeRectXY(shape, effective_radius, effective_radius), fill_paint);
            else {
                m_canvas->drawRect(shape, fill_paint);
            }
        }
        // a transparent or a zero width stroke draws nothing (§6.2)
        if (paint.strokes()) {
            SkPaint stroke_paint;
            stroke_paint.setColor(SkColor(to_skia_color(paint.stroke)));
            stroke_paint.setStyle(SkPaint::kStroke_Style);
            stroke_paint.setStrokeWidth(paint.stroke_width);
            stroke_paint.setAntiAlias(true);
            // the stroke of a plain and of a rounded rectangle differ only in the shape itself (§6.2)
            if (rounded)
                m_canvas->drawRRect(SkRRect::MakeRectXY(shape, effective_radius, effective_radius), stroke_paint);
            else {
                m_canvas->drawRect(shape, stroke_paint);
            }
        }
    }
} // namespace albor::skia
