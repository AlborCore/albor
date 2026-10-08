#include "render/render-skia/skia_render_context.h"

#include <algorithm>
#include <vector>

#include <skia/core/SkBitmap.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkImage.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkPathBuilder.h>
#include <skia/core/SkRRect.h>
#include <skia/core/SkRect.h>
#include <skia/core/SkSamplingOptions.h>
#include <skia/core/SkTypeface.h>

#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"

namespace albor::skia
{
    SkiaRenderContext::SkiaRenderContext(SkCanvas* canvas, float device_scale) :
        m_canvas(canvas), m_device_scale(device_scale) {
        // a context with no canvas draws nothing, so it starts with an empty clip (§6.2)
        if (m_canvas != nullptr) {
            // the canvas spans the physical drawable, so the base scale maps the logical points onto it (§6.3, AD-27)
            m_canvas->scale(m_device_scale, m_device_scale);
            // the clip bookkeeping stays in logical points, which is the space the scene drew in (§6.3, AD-27)
            const SkImageInfo& info = m_canvas->imageInfo();
            // the clip is initially the full canvas in logical points
            m_clip = Rect::from_origin_size(Point{0.0f, 0.0f}, Size{static_cast<float>(info.width()) / m_device_scale, static_cast<float>(info.height()) / m_device_scale});
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
        // the base scale sits under the scene transform, so a logical point still spans the whole drawable (§6.3, AD-27)
        matrix.postScale(m_device_scale, m_device_scale);
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
        // the text takes the fill colour when the paint fills, and the stroke colour otherwise (§6.2)
        SkPaint text_paint;
        text_paint.setColor(SkColor(to_skia_color(paint.fills() ? paint.fill : paint.stroke)));
        text_paint.setAntiAlias(true);
        // the run is the shaped output stage one measured, so the glyphs are replayed (§9.1, AD-18)
        const ShapedRun& run = *layout.run;
        // every line carries its own typeface and its own baseline (§9.1)
        for (const ShapedRun::Line& line : run.lines()) {
            // a line this backend cannot resolve is skipped rather than guessed at (§3.5, §9.1)
            if (!line.font.valid())
                continue;
            // the typeface is retained for the draw, because the handle only borrows it (§9.1)
            sk_sp<SkTypeface> typeface = sk_ref_sp(static_cast<const SkTypeface*>(line.font.object()));
            // the font is built from the typeface and the size the run was shaped at (§9.1)
            SkFont font(typeface, run.font_size());
            // subpixel positioning keeps a fractional origin sharp, which a snapped one would round away (§9.1)
            font.setSubpixel(true);
            // the glyphs and their positions are copied into the spans skia draws (§6.3)
            std::vector<SkGlyphID> glyphs;
            // the positions are parallel to the glyphs, because skia draws one position per glyph (§6.3)
            std::vector<SkPoint> positions;
            // the run already knows how many glyphs the line holds, so both spans are reserved once (§6.3)
            glyphs.reserve(line.glyphs.size());
            positions.reserve(line.glyphs.size());
            // every shaped glyph contributes its index and its position beside the line origin (§9.1)
            for (const ShapedGlyph& glyph : line.glyphs) {
                // the glyph id is narrowed to what skia addresses glyphs with, which is what the visitor reported (§9.1)
                glyphs.push_back(static_cast<SkGlyphID>(glyph.id));
                // the position is taken as skia wants it, relative to the origin the draw is given (§6.3)
                positions.push_back(SkPoint::Make(glyph.position.x, glyph.position.y));
            }
            // the line origin is the baseline, added to the run origin the caller placed the run at (§9.1)
            const SkPoint line_origin = SkPoint::Make(origin.x + line.origin.x, origin.y + line.origin.y);
            // the positioned glyphs are drawn as shaped, so skia rasterizes exactly the glyphs stage one measured (§9.1)
            m_canvas->drawGlyphs(glyphs, positions, line_origin, font, text_paint);
        }
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

    SkMatrix SkiaRenderContext::to_skia_matrix(const Transform& transform) {
        // the matrix mirrors the albor transform with the same column order the scale, skew and translate follow (§6.3)
        SkMatrix matrix;
        matrix.setScale(transform.scale_x, transform.scale_y);
        matrix.setSkewX(transform.skew_x);
        matrix.setSkewY(transform.skew_y);
        matrix.postTranslate(transform.translate_x, transform.translate_y);
        return matrix;
    }

    SkPath SkiaRenderContext::build_path(std::span<const PathVerb> verbs, std::span<const Point> points) {
        // the path starts empty, because a malformed stream is refused before it reaches here (§6.4, AD-06)
        SkPathBuilder builder;
        // the point index advances with every consuming verb (§6.4)
        std::size_t point_index = 0;
        for (const PathVerb verb : verbs) {
            // a move starts a new contour at the next point (§6.4)
            if (verb == PathVerb::MoveTo) {
                builder.moveTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a line draws to the next point (§6.4)
            else if (verb == PathVerb::LineTo) {
                builder.lineTo(points[point_index].x, points[point_index].y);
                point_index++;
            }
            // a close runs the contour back to its start and consumes no point (§6.4)
            else if (verb == PathVerb::Close) {
                builder.close();
            }
        }
        return builder.detach();
    }

    void SkiaRenderContext::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a context with no canvas draws nothing (§6.4)
        if (m_canvas == nullptr)
            return;
        // a batch with no segment draws nothing (§6.4, AD-06)
        if (!batch || batch->segment_count() == 0)
            return;
        // every consecutive point pair lifts to one segment, which the batched replay walks (§6.4)
        SkPathBuilder builder;
        const std::span<const Point> points = batch->points();
        for (std::size_t index = 0; index < points.size(); index += 2) {
            builder.moveTo(points[index].x, points[index].y);
            builder.lineTo(points[index + 1].x, points[index + 1].y);
        }
        const SkPath path = builder.detach();
        // the retained transform concats under the current matrix, so the segments land where the scene placed them (§6.4)
        m_canvas->save();
        m_canvas->concat(to_skia_matrix(transform));
        SkPaint stroke_paint;
        stroke_paint.setStyle(SkPaint::kStroke_Style);
        stroke_paint.setColor(SkColor(to_skia_color(stroke.color)));
        stroke_paint.setStrokeWidth(stroke.width);
        stroke_paint.setAntiAlias(true);
        m_canvas->drawPath(path, stroke_paint);
        m_canvas->restore();
    }

    void SkiaRenderContext::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a context with no canvas draws nothing (§6.4)
        if (m_canvas == nullptr)
            return;
        // a path with no command draws nothing (§6.4, AD-06)
        if (!path || path->verbs().empty())
            return;
        // the contour is rebuilt in one pass, because the resource validates the command stream on creation (§6.4, AD-06)
        const SkPath contour = build_path(path->verbs(), path->points());
        // the retained transform concats under the current matrix, so the contour lands where the scene placed it (§6.4)
        m_canvas->save();
        m_canvas->concat(to_skia_matrix(transform));
        // a transparent fill draws nothing, which is how the paint carries only a stroke (§6.4)
        if (paint.fills()) {
            SkPaint fill_paint;
            fill_paint.setColor(SkColor(to_skia_color(paint.fill)));
            fill_paint.setAntiAlias(true);
            m_canvas->drawPath(contour, fill_paint);
        }
        // a transparent or a zero width stroke draws nothing (§6.4)
        if (paint.strokes()) {
            SkPaint stroke_paint;
            stroke_paint.setColor(SkColor(to_skia_color(paint.stroke)));
            stroke_paint.setStyle(SkPaint::kStroke_Style);
            stroke_paint.setStrokeWidth(paint.stroke_width);
            stroke_paint.setAntiAlias(true);
            m_canvas->drawPath(contour, stroke_paint);
        }
        m_canvas->restore();
    }

    void SkiaRenderContext::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a context with no canvas draws nothing (§6.4)
        if (m_canvas == nullptr)
            return;
        // a picture with no command draws nothing (§6.4, AD-06)
        if (!picture || picture->empty())
            return;
        // the picture transform concats under the current matrix, so the commands land where the scene placed them (§6.4)
        m_canvas->save();
        m_canvas->concat(to_skia_matrix(transform));
        // every command replays in recording order, because the display list is the picture (§6.4)
        for (const PictureCommand& command : picture->commands()) {
            // each command runs under its own recorded transform, which the concat nests under the picture's (§6.4)
            m_canvas->save();
            m_canvas->concat(to_skia_matrix(command.transform));
            switch (command.kind) {
            case PictureCommandKind::Rect:
                // a plain rectangle command draws through the shared shape painter (§6.4)
                paint_shape(command.paint, false, command.rect, 0.0f);
                break;
            case PictureCommandKind::RoundedRect:
                // a rounded rectangle command draws through the shared shape painter (§6.4)
                paint_shape(command.paint, true, command.rect, command.radius);
                break;
            case PictureCommandKind::Text:
                // the text command draws its run at the recorded baseline, under the command transform (§6.4)
                if (!command.text.empty())
                    draw_text(command.text, command.origin, command.paint);
                break;
            case PictureCommandKind::LineBatch:
                // the batch command replays with an identity transform, because the matrix already nests it (§6.4)
                if (command.batch)
                    draw_line_batch(command.batch, Transform{}, command.stroke);
                break;
            case PictureCommandKind::Path:
                // the path command replays with an identity transform, because the matrix already nests it (§6.4)
                if (command.path)
                    draw_path(command.path, Transform{}, command.paint);
                break;
            case PictureCommandKind::Image:
                // the image command draws into the recorded destination, under the command transform (§6.4)
                if (command.image)
                    draw_image(command.image, command.rect, command.paint);
                break;
            }
            m_canvas->restore();
        }
        m_canvas->restore();
    }

    void SkiaRenderContext::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // a context with no canvas draws nothing (§6.4)
        if (m_canvas == nullptr)
            return;
        // an image with no pixel draws nothing (§6.4, AD-06)
        if (!image || image->pixels().empty())
            return;
        // a destination with no area draws nothing (§6.4)
        if (destination.empty())
            return;
        // the raw buffer is an immutable row packed upload in rgba order (§6.4)
        const SkImageInfo info = SkImageInfo::Make(static_cast<int>(image->width()), static_cast<int>(image->height()), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        SkBitmap bitmap;
        // the byte stride of one row is the width times the four channels, which an upload requires (§6.4)
        if (!bitmap.installPixels(info, const_cast<std::uint8_t*>(image->pixels().data()), image->row_bytes()))
            return;
        // the bitmap lifts to an image, which is what the canvas draws (§6.4)
        sk_sp<SkImage> sk_image = SkImages::RasterFromBitmap(bitmap);
        if (!sk_image)
            return;
        // the destination is drawn in the logical space the canvas matrix already maps (§6.4)
        const SkRect dest = SkRect::MakeXYWH(destination.x, destination.y, destination.width, destination.height);
        SkPaint image_paint;
        // the fill colour tints the upload when the paint fills, and the stroke colour otherwise (§6.4)
        if (paint.fills())
            image_paint.setColor(SkColor(to_skia_color(paint.fill)));
        else if (paint.strokes())
            image_paint.setColor(SkColor(to_skia_color(paint.stroke)));
        image_paint.setAntiAlias(true);
        m_canvas->drawImageRect(sk_image, dest, SkSamplingOptions(SkFilterMode::kLinear), &image_paint);
    }
} // namespace albor::skia
