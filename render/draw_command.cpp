#include "render/draw_command.h"

#include <algorithm>
#include <span>

#include <utility>

#include "render/picture_resource.h"
#include "render/render_context.h"

namespace albor
{
    const Rect& DrawCommand::unclipped() {
        // the area reaches far past any window, because the sentinel has to be larger than every surface (§6.2)
        static const Rect area = Rect::from_origin_size(Point{-1.0e6f, -1.0e6f}, Size{2.0e6f, 2.0e6f});
        // the same area is handed out every time, so two unclipped commands compare equal (§6.2)
        return area;
    }

    DrawCommand DrawCommand::rectangle(Rect rect, Paint paint) {
        // the geometry is the rectangle with no radii and no rounding, which is what a plain shape carries (§6.2)
        return DrawCommand(RectGeometry{.rect = rect, .radius = 0.0f, .rounded = false}, paint);
    }

    DrawCommand DrawCommand::rounded_rectangle(Rect rect, float radius, Paint paint) {
        // the geometry says it is rounded, so the kind the replay switches on is the one the caller asked for (§6.2)
        return DrawCommand(RectGeometry{.rect = rect, .radius = radius, .rounded = true}, paint);
    }

    DrawCommand DrawCommand::polyline(std::span<const Point> points, StrokeStyle stroke) {
        // the vertex run and its stroke are the geometry, because a polyline is nothing but the two (§6.2)
        return DrawCommand(PolylineGeometry{.points = std::vector<Point>(points.begin(), points.end()), .stroke = stroke}, Paint{});
    }

    DrawCommand DrawCommand::text(TextLayout layout, Point origin, Paint paint) {
        // the run travels with its box origin, which is what a line of a wrapped run draws at (§6.2, §9.1)
        return DrawCommand(TextGeometry{.layout = std::move(layout), .origin = origin}, paint);
    }

    DrawCommand DrawCommand::line_batch(std::shared_ptr<const LineBatch> batch, Transform transform, StrokeStyle stroke) {
        // the transform is stamped after the build, because the recorder composes it with its own (§6.4)
        DrawCommand command(BatchGeometry{.batch = std::move(batch), .stroke = stroke}, Paint{});
        // the retained geometry draws under the transform the recorder stood at (§6.4)
        command.m_transform = transform;
        return command;
    }

    DrawCommand DrawCommand::path(std::shared_ptr<const PathResource> path, Transform transform, Paint paint) {
        // the transform is stamped after the build, because the recorder composes it with its own (§6.4)
        DrawCommand command(PathGeometry{.path = std::move(path)}, paint);
        // the contour draws under the transform the recorder stood at (§6.4)
        command.m_transform = transform;
        return command;
    }

    DrawCommand DrawCommand::picture(std::shared_ptr<const PictureResource> picture, Transform transform) {
        // the transform is stamped after the build, because the recorder composes it with its own (§6.4)
        DrawCommand command(PictureGeometry{.picture = std::move(picture)}, Paint{});
        // the command list replays under the transform the recorder stood at (§6.4)
        command.m_transform = transform;
        return command;
    }

    DrawCommand DrawCommand::image(std::shared_ptr<const ImageResource> image, Rect destination, Paint paint) {
        // the destination is the geometry, because an image has no shape of its own (§6.4)
        return DrawCommand(ImageGeometry{.image = std::move(image), .destination = destination}, paint);
    }

    DrawCommand DrawCommand::glyph_outline(std::shared_ptr<const GlyphOutline> outline, Point position, Paint paint, Transform transform) {
        // an outline with no points draws nothing, so it is refused rather than recorded (§6.2)
        if (outline == nullptr || outline->empty())
            return DrawCommand(RectGeometry{.rect = Rect::from_origin_size(position, Size{0.0f, 0.0f})}, paint);
        // the outline and its pen are the geometry, because the shape is the retained contour (§6.4, AD-06)
        DrawCommand command(GlyphOutlineGeometry{.outline = std::move(outline), .position = position}, paint);
        // the transform is stamped like every retained draw, so a scaled subtree scales the glyph (§6.4, §6.1)
        command.set_transform(transform);
        return command;
    }

    DrawCommand DrawCommand::shadow(Rect rect, float radius, Shadow shadow) {
        // a shadow with no area casts nothing, so it is refused rather than recorded (§6.2)
        if (rect.empty() || !shadow.casts())
            return DrawCommand(RectGeometry{.rect = Rect{}}, Paint{});
        // the casting shape and the cast are the geometry, because the blur needs both (§6.2)
        return DrawCommand(ShadowGeometry{.rect = rect, .radius = radius, .shadow = shadow}, Paint{});
    }

    DrawCommand::DrawCommand(Geometry geometry, Paint paint) :
        m_clip(unclipped()), m_paint(paint), m_geometry(std::move(geometry)) {}

    DrawCommandKind DrawCommand::kind() const {
        // the alternative names its kind, so the enum and the variant need not share an order (§6.2)
        switch (m_geometry.index()) {
        // one shape geometry carries both kinds, and the flag says which one was asked for (§6.2)
        case 0:
            return rect_geometry().rounded ? DrawCommandKind::RoundedRect : DrawCommandKind::Rect;
        // every other alternative is a kind of its own (§6.2)
        case 1:
            return DrawCommandKind::Text;
        // a batch is the third alternative (§6.4)
        case 2:
            return DrawCommandKind::LineBatch;
        // a contour is the fourth alternative (§6.4)
        case 3:
            return DrawCommandKind::Path;
        // a picture is the fifth alternative (§6.4)
        case 4:
            return DrawCommandKind::Picture;
        // an image is the sixth alternative (§6.4)
        case 5:
            return DrawCommandKind::Image;
        // a glyph outline is the seventh alternative (§6.4, AD-34)
        case 6:
            return DrawCommandKind::GlyphOutline;
        // a shadow is the eighth alternative, which is what a variant holds at most (§6.2)
        case 7:
            return DrawCommandKind::Shadow;
        // a polyline is the ninth alternative, which is what a variant holds at most (§6.2)
        case 8:
            return DrawCommandKind::Polyline;
        // an alternative outside the enumeration is read as the shadow, which a report sees (§6.2)
        default:
            return DrawCommandKind::Shadow;
        }
    }

    const Rect& DrawCommand::clip() const {
        // the clip travels with the command, because stage two cannot ask where it came from (§6.1, AD-18)
        return m_clip;
    }

    const Transform& DrawCommand::transform() const {
        // the transform is what retained geometry draws under (§6.4)
        return m_transform;
    }

    const Paint& DrawCommand::paint() const {
        // the paint travels with the geometry, because stage two cannot ask what the ui thread painted (§6.1, AD-18)
        return m_paint;
    }

    const RectGeometry& DrawCommand::rect_geometry() const {
        // the geometry is a shape only when the command is one, which the replay guarantees (§6.2)
        return std::get<RectGeometry>(m_geometry);
    }

    const PolylineGeometry& DrawCommand::polyline_geometry() const {
        // the geometry is a polyline only when the command is one, which the replay guarantees (§6.2)
        return std::get<PolylineGeometry>(m_geometry);
    }

    const TextGeometry& DrawCommand::text_geometry() const {
        // the geometry is a run only when the command is one, which the replay guarantees (§6.2)
        return std::get<TextGeometry>(m_geometry);
    }

    const BatchGeometry& DrawCommand::batch_geometry() const {
        // the geometry is a batch only when the command is one, which the replay guarantees (§6.4)
        return std::get<BatchGeometry>(m_geometry);
    }

    const PathGeometry& DrawCommand::path_geometry() const {
        // the geometry is a contour only when the command is one, which the replay guarantees (§6.4)
        return std::get<PathGeometry>(m_geometry);
    }

    const PictureGeometry& DrawCommand::picture_geometry() const {
        // the geometry is a picture only when the command is one, which the replay guarantees (§6.4)
        return std::get<PictureGeometry>(m_geometry);
    }

    const GlyphOutlineGeometry& DrawCommand::glyph_outline_geometry() const {
        // the outline is the alternative this command was built with (§6.4, AD-34)
        return std::get<GlyphOutlineGeometry>(m_geometry);
    }

    const ImageGeometry& DrawCommand::image_geometry() const {
        // the geometry is an image only when the command is one, which the replay guarantees (§6.4)
        return std::get<ImageGeometry>(m_geometry);
    }

    const ShadowGeometry& DrawCommand::shadow_geometry() const {
        // the cast is the alternative this command was built with (§6.2)
        return std::get<ShadowGeometry>(m_geometry);
    }

    void DrawCommand::set_clip_radius(float radius) {
        // the radius rides with the clip, which the replay rounds the scissor by (§6.2)
        m_clip_radius = radius;
    }

    float DrawCommand::clip_radius() const {
        // the recorded radius is what a replay reads (§6.2)
        return m_clip_radius;
    }

    void DrawCommand::set_clip(Rect clip) {
        // the clip is stamped before the command joins the list, so it is fixed for good (§6.2)
        m_clip = clip;
    }

    void DrawCommand::set_transform(Transform transform) {
        // the transform is stamped before the command joins the list, so it is fixed for good (§6.4)
        m_transform = transform;
    }

    void DrawCommand::scale_alpha(float factor) {
        // a cast carries its colour in its own geometry, because a shadow command has no paint of its own (§6.2)
        if (auto* cast = std::get_if<ShadowGeometry>(&m_geometry)) {
            // the cast takes the factor once, because it is the same coverage multiplier the paint would take (§6.2)
            cast->shadow.color.alpha = std::clamp(cast->shadow.color.alpha * factor, 0.0f, 1.0f);
            return;
        }
        // a polyline carries its colour in its own geometry, because its stroke is what it draws with (§6.2)
        if (auto* line = std::get_if<PolylineGeometry>(&m_geometry)) {
            // the glyph stroke takes the factor once, which is what an opacity dims a check mark by (§6.1)
            line->stroke.color.alpha = std::clamp(line->stroke.color.alpha * factor, 0.0f, 1.0f);
            return;
        }
        // both channels move together, because an opacity dims a fill and a stroke alike (§6.1)
        m_paint.fill.alpha = std::clamp(m_paint.fill.alpha * factor, 0.0f, 1.0f);
        // the stroke carries its own alpha, so it takes the same factor rather than the fill's result (§6.1)
        m_paint.stroke.alpha = std::clamp(m_paint.stroke.alpha * factor, 0.0f, 1.0f);
    }

    bool DrawCommand::draws_nothing() const {
        // a command under an empty clip can put no ink down, so a recorder culls it (§6.2)
        if (m_clip.empty())
            return true;
        // a paint that neither fills nor strokes draws nothing, whatever the shape (§6.2)
        const bool paints = m_paint.fills() || m_paint.strokes();
        // the switch asks each geometry whether it has anything to draw (§6.2)
        switch (kind()) {
        // a shape with no area or no paint contributes nothing to the frame (§6.2)
        case DrawCommandKind::Rect:
        case DrawCommandKind::RoundedRect:
            return rect_geometry().rect.empty() || !paints;
        // a polyline with fewer than two vertices, or a stroke of no width, contributes nothing (§6.2)
        case DrawCommandKind::Polyline: {
            // the vertices and their stroke are read together, because both decide what is drawn (§6.2)
            const PolylineGeometry& line = polyline_geometry();
            return line.points.size() < 2 || line.stroke.width <= 0.0f;
        }
        // a run with no glyphs or no paint contributes nothing to the frame (§6.2)
        case DrawCommandKind::Text:
            return text_geometry().layout.empty() || !paints;
        // a batch with no segment, or a stroke of no width, contributes nothing (§6.4)
        case DrawCommandKind::LineBatch: {
            // the batch and its stroke are read together, because both decide what is drawn (§6.4)
            const BatchGeometry& batch = batch_geometry();
            return !batch.batch || batch.batch->segment_count() == 0 || batch.stroke.width <= 0.0f;
        }
        // a contour with no command, or no paint, contributes nothing (§6.4)
        case DrawCommandKind::Path: {
            // the contour is read alone, because a missing one leaves nothing to paint (§6.4)
            const PathGeometry& contour = path_geometry();
            return !contour.path || contour.path->verbs().empty() || !paints;
        }
        // a picture with no command contributes nothing, and a picture carries its own paint (§6.4)
        case DrawCommandKind::Picture: {
            // the picture is read alone, because a missing one leaves nothing to replay (§6.4)
            const PictureGeometry& picture = picture_geometry();
            return !picture.picture || picture.picture->empty();
        }
        // an image with no pixel, or a destination with no area, contributes nothing (§6.4)
        case DrawCommandKind::Image: {
            // the pixels and the destination are read together, because both decide what is drawn (§6.4)
            const ImageGeometry& image = image_geometry();
            return !image.image || image.image->pixels().empty() || image.destination.empty();
        }
        // an outline with no point, or no paint, contributes nothing (§6.4, AD-34)
        case DrawCommandKind::GlyphOutline: {
            // the outline is read alone, because a missing one leaves nothing to trace (§6.4)
            const GlyphOutlineGeometry& outline = glyph_outline_geometry();
            return !outline.outline || outline.outline->empty() || !paints;
        }
        // a shadow casts nothing without a shape or a colour of its own (§6.2)
        case DrawCommandKind::Shadow: {
            // the cast is read alone, because it carries its own paint rather than the command's (§6.2)
            const ShadowGeometry& cast = shadow_geometry();
            return cast.rect.empty() || !cast.shadow.casts();
        }
        }
        // the switch covers every kind, so nothing reaches here (§6.2)
        return true;
    }

    Rect DrawCommand::bounds() const {
        // the geometry kind decides what the command could touch, because no single accessor spans them (§6.2)
        switch (kind()) {
        case DrawCommandKind::Rect:
        case DrawCommandKind::RoundedRect:
            // a rectangle covers exactly the rectangle it was given (§6.2)
            return rect_geometry().rect;
        case DrawCommandKind::Polyline: {
            // a polyline is bounded by its own vertices, which is what the run carries (§6.2)
            const std::vector<Point>& points = polyline_geometry().points;
            // a run with no vertex covers nothing, which is the quiet default (§6.2)
            if (points.empty())
                return Rect{};
            // the edges start at the first vertex, which every run has (§6.2)
            float left = points.front().x;
            float right = left;
            float top = points.front().y;
            float bottom = top;
            // every further vertex widens the edges, which is what makes the bound conservative (§6.2)
            for (const Point& point : points) {
                left = std::min(left, point.x);
                right = std::max(right, point.x);
                top = std::min(top, point.y);
                bottom = std::max(bottom, point.y);
            }
            // the bound is returned whole (§6.2)
            return Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
        }
        case DrawCommandKind::Text: {
            // a run covers its measured extent, which the layout already carries (§9.1)
            const TextLayout& layout = text_geometry().layout;
            // the size is measured, so the bound is the origin at zero with that extent (§6.2, §9.1)
            return Rect{.x = 0.0f, .y = 0.0f, .width = layout.size.width, .height = layout.size.height};
        }
        case DrawCommandKind::LineBatch:
            // a batch has no single rectangle of its own, so it covers the clip it draws under (§6.2)
            return clip();
        case DrawCommandKind::Path: {
            // a contour is bounded by its own points, which the resource carries (§6.4)
            const std::shared_ptr<const PathResource>& path = path_geometry().path;
            // a missing contour covers nothing, which is the quiet default (§6.4)
            if (path == nullptr || path->points().empty())
                return Rect{};
            // the left edge starts at the first point, which every contour has (§6.4)
            float left = path->points().front().x;
            // the right edge starts at the same point (§6.4)
            float right = left;
            // the top edge starts at the same point (§6.4)
            float top = path->points().front().y;
            // the bottom edge starts at the same point (§6.4)
            float bottom = top;
            // every further point widens the edges, which is what makes the bound conservative (§6.4)
            for (const Point& point : path->points()) {
                // the left edge follows the leftmost point (§6.4)
                left = std::min(left, point.x);
                // the right edge follows the rightmost (§6.4)
                right = std::max(right, point.x);
                // the top edge follows the topmost (§6.4)
                top = std::min(top, point.y);
                // the bottom edge follows the bottommost (§6.4)
                bottom = std::max(bottom, point.y);
            }
            // the bound is returned whole (§6.4)
            return Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
        }
        case DrawCommandKind::Picture:
            // a picture may draw anything, so it covers the clip entirely (§6.2)
            return clip();
        case DrawCommandKind::Image:
            // an image covers exactly where it was placed (§6.2)
            return image_geometry().destination;
        case DrawCommandKind::GlyphOutline: {
            // an outline covers its own points placed at its pen and under its transform (§6.4, §6.2)
            const GlyphOutlineGeometry& geometry = glyph_outline_geometry();
            // a missing outline covers nothing, which is the quiet default (§6.4)
            if (geometry.outline == nullptr || geometry.outline->points().empty())
                return Rect{};
            // a point of the glyph is placed at the pen first, then under the command's transform (§6.4, §6.1)
            const auto placed_point = [this, &geometry](const Point& point) { return m_transform.apply(Point{.x = geometry.position.x + point.x, .y = geometry.position.y + point.y}); };
            // the first point starts every edge, and it is placed once because the transform is not free (§6.4)
            const Point first = placed_point(geometry.outline->points().front());
            // the left edge starts at the first point, placed (§6.4)
            float left = first.x;
            // the right edge starts at the same point (§6.4)
            float right = left;
            // the top edge starts at the same point (§6.4)
            float top = first.y;
            // the bottom edge starts at the same point (§6.4)
            float bottom = top;
            // every further point widens the edges, which is what makes the bound conservative (§6.4)
            for (const Point& point : geometry.outline->points()) {
                // the point is placed where the replay draws it, because a skewed outline covers elsewhere (§6.1)
                const Point drawn = placed_point(point);
                // the left edge follows the leftmost point (§6.4)
                left = std::min(left, drawn.x);
                // the right edge follows the rightmost (§6.4)
                right = std::max(right, drawn.x);
                // the top edge follows the topmost (§6.4)
                top = std::min(top, drawn.y);
                // the bottom edge follows the bottommost (§6.4)
                bottom = std::max(bottom, drawn.y);
            }
            // the bound is returned whole (§6.4)
            return Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
        }
        case DrawCommandKind::Shadow:
            return shadow_bounds();
        }
        // a kind outside the enumeration covers nothing, which is the quiet default (§6.2)
        return Rect{};
    }

    Rect DrawCommand::shadow_bounds() const {
        // the cast is read once, because both the shape and the reach come from it (§6.2)
        const ShadowGeometry& cast = shadow_geometry();
        // three sigmas is what a blur of this radius reaches, so the bound is conservative rather than tight (§6.2)
        const float reach = cast.shadow.blur * 3.0f;
        // the offset moves the whole cast, because that is what a shadow offset means (§6.2)
        return Rect{.x = cast.rect.x + cast.shadow.offset.x - reach, .y = cast.rect.y + cast.shadow.offset.y - reach, .width = cast.rect.width + reach * 2.0f, .height = cast.rect.height + reach * 2.0f};
    }

    void DrawCommand::replay_glyph_outline(RenderContext& context) const {
        // the outline carries every contour of the glyph, whose path was built once with it (§6.4, AD-06)
        const GlyphOutlineGeometry& geometry = glyph_outline_geometry();
        // an outline of fewer than three points encloses no area, so there is nothing to draw (§6.2)
        if (geometry.outline->points().size() < 3)
            return;
        // the pen places the glyph, and the command's transform places the pen (§6.4, §6.1)
        const Transform pen{.translate_x = geometry.position.x, .translate_y = geometry.position.y};
        // the retained path draws under both, so a scaled subtree scales its glyphs (§6.4, AD-06)
        context.draw_path(geometry.outline->path(), pen.then(m_transform), m_paint);
    }

    void DrawCommand::replay(RenderContext& context) const {
        // the switch is the one place a kind is drawn, which keeps a frame and a picture in agreement (§6.1, §6.4)
        switch (kind()) {
        // a shape draws through its own primitive, which keeps the radii a plain rectangle has no use for (§6.2)
        case DrawCommandKind::Rect:
            context.draw_rect(rect_geometry().rect, m_paint);
            return;
        // a rounded shape draws through the primitive that keeps its radii (§6.2)
        case DrawCommandKind::RoundedRect:
            context.draw_rounded_rect(rect_geometry().rect, rect_geometry().radius, m_paint);
            return;
        // a polyline draws through the primitive that joins its vertices (§6.2)
        case DrawCommandKind::Polyline:
            context.draw_polyline(polyline_geometry().points, polyline_geometry().stroke);
            return;
        // a run draws at the origin of its box, which every line places its own baseline under (§6.2, §9.1)
        case DrawCommandKind::Text:
            context.draw_text(text_geometry().layout, text_geometry().origin, m_paint);
            return;
        // a batch draws under the transform the recorder composed for it (§6.4)
        case DrawCommandKind::LineBatch:
            context.draw_line_batch(batch_geometry().batch, m_transform, batch_geometry().stroke);
            return;
        // a contour draws under the transform the recorder composed for it (§6.4)
        case DrawCommandKind::Path:
            context.draw_path(path_geometry().path, m_transform, m_paint);
            return;
        // a picture replays its own commands under the transform the recorder composed for it (§6.4)
        case DrawCommandKind::Picture:
            context.draw_picture(picture_geometry().picture, m_transform);
            return;
        // an image draws into the destination the recorder recorded (§6.4)
        case DrawCommandKind::Image:
            context.draw_image(image_geometry().image, image_geometry().destination, m_paint);
            return;
        // an embedded glyph draws as its tessellated outline, which is how AD-34 text reaches a surface (§6.4, AD-34)
        case DrawCommandKind::GlyphOutline:
            replay_glyph_outline(context);
            return;
        // a shadow is drawn through the primitive that blurs offscreen and composites (§6.2)
        case DrawCommandKind::Shadow:
            context.draw_shadow(shadow_geometry().rect, shadow_geometry().radius, shadow_geometry().shadow);
            return;
        }
    }
    void replay_commands(const std::vector<DrawCommand>& commands, RenderContext& context, const Rect& unclipped) {
        // the applied clip starts as the whole area, which is what the commands of an unclipped tree record (§6.2)
        Rect applied = unclipped;
        // the applied radius starts at zero, because the whole area carries no corners (§6.2)
        float applied_radius = 0.0f;
        // the switch is closed until a command asks for a narrower clip (§6.2)
        bool switched = false;

        // every command replays in paint order, which is the order the ui thread recorded (§6.1)
        for (const DrawCommand& command : commands) {
            // a narrower clip opens a switch, which is balanced again when the next command differs (§6.2)
            if (command.clip() != applied || command.clip_radius() != applied_radius) {
                // the previous switch is closed first, because a clip intersects rather than replaces (§6.2)
                if (switched)
                    context.restore();
                // the new clip is pushed, so it intersects whatever was already applied (§6.2)
                context.save();
                // the command's own clip is applied, which is what stage one recorded (§6.2)
                if (command.clip_radius() > 0.0f)
                    context.clip_rounded_rect(command.clip(), command.clip_radius());
                else
                    context.clip_rect(command.clip());
                // the applied clip is remembered, so a run of commands under it costs nothing more (§6.2)
                applied = command.clip();
                applied_radius = command.clip_radius();
                // the switch is open, which the closing branch above balances (§6.2)
                switched = true;
            }
            // the command draws itself, which is the one place a kind is drawn (§6.1, §6.4)
            command.replay(context);
        }

        // an open switch is closed last, because the whole list has to balance its own state (§6.2)
        if (switched)
            context.restore();
    }
} // namespace albor
