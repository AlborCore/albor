#include "render/draw_command.h"

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

    DrawCommand DrawCommand::glyph_outline(std::shared_ptr<const GlyphOutline> outline, Point position, Paint paint) {
        // an outline with no points draws nothing, so it is refused rather than recorded (§6.2)
        if (outline == nullptr || outline->empty())
            return DrawCommand(RectGeometry{.rect = Rect::from_origin_size(position, Size{0.0f, 0.0f})}, paint);
        // the outline and where it lands are the geometry, because the shape is the retained contour (§6.4, AD-06)
        return DrawCommand(GlyphOutlineGeometry{.outline = std::move(outline), .position = position}, paint);
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
        // a glyph outline is the last alternative, which is what a variant holds at most (§6.4, AD-34)
        default:
            return DrawCommandKind::GlyphOutline;
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

    void DrawCommand::set_clip(Rect clip) {
        // the clip is stamped before the command joins the list, so it is fixed for good (§6.2)
        m_clip = clip;
    }

    void DrawCommand::set_transform(Transform transform) {
        // the transform is stamped before the command joins the list, so it is fixed for good (§6.4)
        m_transform = transform;
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
        }
        // the switch covers every kind, so nothing reaches here (§6.2)
        return true;
    }

    void DrawCommand::replay_glyph_outline(RenderContext& context) const {
        // the outline is a single closed contour, so its verbs are one move followed by one line per point (§6.4)
        const GlyphOutlineGeometry& geometry = glyph_outline_geometry();
        const std::vector<Point>& points = geometry.outline->points();
        // a contour of fewer than three points encloses nothing, so there is nothing to draw (§6.2)
        if (points.size() < 3)
            return;
        // the verbs are built beside the call because they describe a closed contour rather than being stored (§6.4)
        std::vector<PathVerb> verbs;
        // the verb list matches the point list exactly, because the two are paired by index (§6.4)
        verbs.reserve(points.size());
        // the first point starts the contour, and every point after it is a straight segment (§6.4)
        verbs.push_back(PathVerb::MoveTo);
        // the remaining points are lines, because a glyph outline is tessellated into straight segments (§6.4, §15)
        for (std::size_t index = 1; index < points.size(); index++)
            verbs.push_back(PathVerb::LineTo);
        // the identifier is the outline's own hash, because the shape is what the path is keyed on (§6.4, AD-06)
        const std::uint64_t identifier = geometry.outline->font_hash() ^ (static_cast<std::uint64_t>(geometry.outline->glyph_id()) << 1);
        // the path is retained for the duration of the draw, because a backend reads it synchronously (§6.4)
        const std::shared_ptr<const PathResource> path = std::make_shared<const PathResource>(identifier, std::span<const PathVerb>(verbs), std::span<const Point>(points));
        // the outline draws as a filled path at the pen position, which is the same fill a glyph rasterizes with (§6.4)
        const Transform placed{.translate_x = geometry.position.x, .translate_y = geometry.position.y};
        // the contour draws under that placement, so the outline lands where the glyph sat (§9.1)
        context.draw_path(path, placed, m_paint);
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
        }
    }
    void replay_commands(const std::vector<DrawCommand>& commands, RenderContext& context, const Rect& unclipped) {
        // the applied clip starts as the whole area, which is what the commands of an unclipped tree record (§6.2)
        Rect applied = unclipped;
        // the switch is closed until a command asks for a narrower clip (§6.2)
        bool switched = false;

        // every command replays in paint order, which is the order the ui thread recorded (§6.1)
        for (const DrawCommand& command : commands) {
            // a narrower clip opens a switch, which is balanced again when the next command differs (§6.2)
            if (command.clip() != applied) {
                // the previous switch is closed first, because a clip intersects rather than replaces (§6.2)
                if (switched)
                    context.restore();
                // the new clip is pushed, so it intersects whatever was already applied (§6.2)
                context.save();
                // the command's own clip is applied, which is what stage one recorded (§6.2)
                context.clip_rect(command.clip());
                // the applied clip is remembered, so a run of commands under it costs nothing more (§6.2)
                applied = command.clip();
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
