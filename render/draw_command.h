#pragma once

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/glyph_outline.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/text_layout.h"

namespace albor
{
    // the drawing surface a command replays into, so this header names the contract without its includes (§3.5)
    class RenderContext;

    // the retained immutable resource a picture command replays, which the factory stamps (§6.4, AD-06)
    class PictureResource;

    // which geometry a command carries, which the replay switches on (§6.2, §6.4)
    enum class DrawCommandKind
    {
        // a filled and stroked rectangle (§6.2)
        Rect,

        // a filled and stroked rectangle with corner radii (§6.2)
        RoundedRect,

        // a polyline joined from vertex to vertex, which is what a control glyph strokes (§6.2)
        Polyline,

        // a shaped text run (§6.2)
        Text,

        // a retained line batch under a transform (§6.4, AD-06)
        LineBatch,

        // a retained path under a transform (§6.4, AD-06)
        Path,

        // a retained picture under a transform (§6.4, AD-06)
        Picture,

        // a retained image into a destination rectangle (§6.4, AD-06)
        Image,

        // one glyph of an embedded font, drawn as its tessellated outline (§6.4, AD-06, AD-34)
        GlyphOutline,

        // a shadow cast by a rounded shape, which the replay blurs offscreen and composites (§6.2)
        Shadow,
    };

    // the geometry a rectangle command carries, which is the rectangle and its corner radii (§6.2)
    struct RectGeometry
    {
        // the rectangle the shape occupies (§6.2)
        Rect rect;

        // the corner radii, which are zero for a plain rectangle (§6.2)
        float radius = 0.0f;

        // whether the shape was asked for as rounded, which is what the kind is read from (§6.2)
        bool rounded = false;

        // compare two shapes component wise, which a command comparison needs (§6.2)
        friend bool operator==(const RectGeometry& lhs, const RectGeometry& rhs) = default;
    };

    // the geometry a polyline command carries, which is the vertex run and its stroke (§6.2)
    struct PolylineGeometry
    {
        // the vertices in drawing order, which the replay joins into one contour (§6.2)
        std::vector<Point> points;

        // the stroke the polyline draws with (§6.2)
        StrokeStyle stroke;

        // compare two polylines component wise, which a command comparison needs (§6.2)
        friend bool operator==(const PolylineGeometry& lhs, const PolylineGeometry& rhs) = default;
    };

    // the geometry a text command carries, which is the run and the origin of its box (§6.2)
    struct TextGeometry
    {
        // the measured run, which the draw replays verbatim (§9.1)
        TextLayout layout;

        // the top left of the box the run occupies (§6.2, §9.1)
        Point origin;

        // compare two runs component wise, which a command comparison needs (§6.2)
        friend bool operator==(const TextGeometry& lhs, const TextGeometry& rhs) = default;
    };

    // the geometry a line batch command carries, which is the batch and its stroke (§6.4)
    struct BatchGeometry
    {
        // the immutable segments the command replays (§6.4, AD-06)
        std::shared_ptr<const LineBatch> batch;

        // the stroke the segments draw with (§6.4)
        StrokeStyle stroke;

        // compare two batches component wise, which a command comparison needs (§6.4)
        friend bool operator==(const BatchGeometry& lhs, const BatchGeometry& rhs) = default;
    };

    // the geometry a path command carries, which is the retained contour (§6.4)
    struct PathGeometry
    {
        // the immutable contour the command replays (§6.4, AD-06)
        std::shared_ptr<const PathResource> path;

        // compare two contours component wise, which a command comparison needs (§6.4)
        friend bool operator==(const PathGeometry& lhs, const PathGeometry& rhs) = default;
    };

    // the geometry a picture command carries, which is the retained command list (§6.4)
    struct PictureGeometry
    {
        // the immutable command list the command replays (§6.4, AD-06)
        std::shared_ptr<const PictureResource> picture;

        // compare two pictures component wise, which a command comparison needs (§6.4)
        friend bool operator==(const PictureGeometry& lhs, const PictureGeometry& rhs) = default;
    };

    // the geometry an image command carries, which is the pixels and where they land (§6.4)
    struct ImageGeometry
    {
        // the immutable pixels the command uploads (§6.4, AD-06)
        std::shared_ptr<const ImageResource> image;

        // the rectangle the image fills (§6.4)
        Rect destination;

        // compare two images component wise, which a command comparison needs (§6.4)
        friend bool operator==(const ImageGeometry& lhs, const ImageGeometry& rhs) = default;
    };

    // the geometry a glyph outline command carries, which is the outline and where it lands (§6.4)
    struct GlyphOutlineGeometry
    {
        // the tessellated outline the replay draws, which is retained rather than rebuilt (§6.4, AD-06)
        std::shared_ptr<const GlyphOutline> outline;

        // the pen position the glyph sits at, which the outline is drawn relative to (§9.1)
        Point position;

        // compare two outlines component wise, which a command comparison needs (§6.2)
        friend bool operator==(const GlyphOutlineGeometry& lhs, const GlyphOutlineGeometry& rhs) = default;
    };

    // the geometry a shadow command carries, which is the shape that casts and how the cast falls (§6.2)
    struct ShadowGeometry
    {
        // the rectangle the cast shape occupies, which is the shape before it is offset (§6.2)
        Rect rect;

        // the corner radius of the casting shape, which the blur follows (§6.2)
        float radius = 0.0f;

        // the colour, offset and blur the cast is drawn with (§6.2)
        Shadow shadow;

        // compare two shadows component wise, which a command comparison needs (§6.2)
        friend bool operator==(const ShadowGeometry& lhs, const ShadowGeometry& rhs) = default;
    };

    // one draw of a frame scene, which is an ordered list of these so paint order survives the hand off (§6.1, AD-18)
    class DrawCommand
    {
    public:
        // the geometry a command carries, where the alternative index is the kind (§6.2)
        using Geometry = std::variant<RectGeometry, TextGeometry, BatchGeometry, PathGeometry, PictureGeometry, ImageGeometry, GlyphOutlineGeometry, ShadowGeometry, PolylineGeometry>;

        // the clip that stands for no clip at all, which is what a command carries until a recorder stamps one (§6.2)
        [[nodiscard]] static const Rect& unclipped();

        // build a filled or stroked rectangle command (§6.2)
        [[nodiscard]] static DrawCommand rectangle(Rect rect, Paint paint);

        // build a rounded rectangle command, which keeps the radius the recorder was given (§6.2)
        [[nodiscard]] static DrawCommand rounded_rectangle(Rect rect, float radius, Paint paint);

        // build a polyline command over the vertex run, which carries its stroke (§6.2)
        [[nodiscard]] static DrawCommand polyline(std::span<const Point> points, StrokeStyle stroke);

        // build a text command with the origin of the run box (§6.2)
        [[nodiscard]] static DrawCommand text(TextLayout layout, Point origin, Paint paint);

        // build a retained line batch command (§6.4, AD-06)
        [[nodiscard]] static DrawCommand line_batch(std::shared_ptr<const LineBatch> batch, Transform transform, StrokeStyle stroke);

        // build a retained path command (§6.4, AD-06)
        [[nodiscard]] static DrawCommand path(std::shared_ptr<const PathResource> path, Transform transform, Paint paint);

        // build a retained picture command (§6.4, AD-06)
        [[nodiscard]] static DrawCommand picture(std::shared_ptr<const PictureResource> picture, Transform transform);

        // build a retained image command (§6.4, AD-06)
        [[nodiscard]] static DrawCommand image(std::shared_ptr<const ImageResource> image, Rect destination, Paint paint);

        // build a glyph outline command, which is how embedded text reaches the surface (§6.4, AD-34)
        [[nodiscard]] static DrawCommand glyph_outline(std::shared_ptr<const GlyphOutline> outline, Point position, Paint paint, Transform transform = Transform{});

        // build a shadow command, which the replay blurs offscreen and composites under the shape (§6.2)
        [[nodiscard]] static DrawCommand shadow(Rect rect, float radius, Shadow shadow);

        // the kind of geometry the command carries (§6.2)
        [[nodiscard]] DrawCommandKind kind() const;

        // the clip the command draws under, which the replay scissors to (§6.2)
        [[nodiscard]] const Rect& clip() const;

        // the area the command covers, which an overlay bounds or a debugger measures (§6.2, §12.2)
        [[nodiscard]] Rect bounds() const;

        // the transform the retained geometry draws under, which a picture command nests under (§6.4)
        [[nodiscard]] const Transform& transform() const;

        // the fill and stroke of the shape, run or image (§6.2)
        [[nodiscard]] const Paint& paint() const;

        // the rectangle and corner radii of a shape command (§6.2)
        [[nodiscard]] const RectGeometry& rect_geometry() const;

        // the vertex run and stroke of a polyline command (§6.2)
        [[nodiscard]] const PolylineGeometry& polyline_geometry() const;

        // the run and the origin of its box of a text command (§6.2)
        [[nodiscard]] const TextGeometry& text_geometry() const;

        // the batch and its stroke of a line batch command (§6.4)
        [[nodiscard]] const BatchGeometry& batch_geometry() const;

        // the contour of a path command (§6.4)
        [[nodiscard]] const PathGeometry& path_geometry() const;

        // the command list of a picture command (§6.4)
        [[nodiscard]] const PictureGeometry& picture_geometry() const;

        // the pixels and the destination of an image command (§6.4)
        [[nodiscard]] const ImageGeometry& image_geometry() const;

        // the outline and the position of a glyph outline command (§6.4, AD-34)
        [[nodiscard]] const GlyphOutlineGeometry& glyph_outline_geometry() const;

        // the casting shape and the cast of a shadow command (§6.2)
        [[nodiscard]] const ShadowGeometry& shadow_geometry() const;

        // stamp the clip the recorder stood at, which is what the replay scissors to (§6.2)
        void set_clip(Rect clip);

        // stamp the corner radius of the clip, which zero leaves it rectangular (§6.2)
        void set_clip_radius(float radius);

        // hand out the corner radius of the clip, which the replay rounds the scissor by (§6.2)
        [[nodiscard]] float clip_radius() const;

        // stamp the transform the recorder stood at, which retained geometry draws under (§6.4)
        void set_transform(Transform transform);

        // scale the alpha of both paint channels by a fraction, which is how a node opacity reaches the surface (§6.1)
        void scale_alpha(float factor);

        // report whether the command would draw nothing at all, which is what a recorder culls (§6.2)
        [[nodiscard]] bool draws_nothing() const;

        // draw the command into a context, which is the one replay the frame and a picture share (§6.1, §6.4)
        void replay(RenderContext& context) const;

        // draw one embedded glyph as its outline, which builds a contour and so is separate (§6.4, AD-34)
        void replay_glyph_outline(RenderContext& context) const;

        // compare two commands field by field (§6.2)
        friend bool operator==(const DrawCommand& lhs, const DrawCommand& rhs) = default;

        // compare two commands for inequality, which a test asserts on (§6.2)
        friend bool operator!=(const DrawCommand& lhs, const DrawCommand& rhs) = default;

    private:
        // read the area a cast covers, which is the shape plus the reach of its blur (§6.2)
        [[nodiscard]] Rect shadow_bounds() const;

        // build a command over its geometry, so the clip and the transform are stamped after (§6.2)
        explicit DrawCommand(Geometry geometry, Paint paint);

        // the clip the command draws under, which is the unclipped one until a recorder stamps it (§6.2)
        Rect m_clip;

        // the corner radius of the clip, which zero leaves it rectangular (§6.2)
        float m_clip_radius = 0.0f;

        // the transform the retained geometry draws under (§6.4)
        Transform m_transform;

        // the fill and stroke of the shape, run or image (§6.2)
        Paint m_paint;

        // the geometry the command carries, where the alternative index is the kind (§6.2)
        Geometry m_geometry;
    };

    // replay a command list in paint order, which is the one replay a frame and a picture share (§6.1, §6.4)
    void replay_commands(const std::vector<DrawCommand>& commands, RenderContext& context, const Rect& unclipped);
} // namespace albor
