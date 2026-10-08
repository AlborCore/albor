#pragma once

#include <memory>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/frame_scene.h"

namespace albor
{
    // a retained immutable resource the scene build passes down, which the backend replays in constant time (§6.4, AD-06)
    class LineBatch;
    class PathResource;
    class PictureResource;
    class ImageResource;

    // the two dimensional drawing surface a renderer backend hands to the scene build (§6.2)
    class RenderContext
    {
    public:
        virtual ~RenderContext() = default;

        // push the current state, transform and clip onto the state stack (§6.2)
        virtual void save() = 0;

        // pop the state the matching save pushed (§6.2)
        virtual void restore() = 0;

        // replace the current transform (§6.2)
        virtual void set_transform(const Transform& transform) = 0;

        // read back the current transform, which a test asserts against a test double (§6.2)
        [[nodiscard]] virtual const Transform& transform() const = 0;

        // intersect the clip with an axis aligned rectangle (§6.2)
        virtual void clip_rect(const Rect& rect) = 0;

        // intersect the clip with a rounded rectangle (§6.2)
        virtual void clip_rounded_rect(const Rect& rect, float radius) = 0;

        // read back the current clip, which a test asserts against a test double (§6.2)
        [[nodiscard]] virtual const Rect& clip() const = 0;

        // draw a filled and stroked rectangle (§6.2)
        virtual void draw_rect(const Rect& rect, const Paint& paint) = 0;

        // draw a filled and stroked rounded rectangle (§6.2)
        virtual void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) = 0;

        // draw a shaped text run whose origin is the top left of its box (§6.2, §9.1)
        virtual void draw_text(const TextLayout& layout, Point origin, const Paint& paint) = 0;

        // draw a retained immutable line batch under a transform, which replays without rebuilding geometry (§6.4, AD-06)
        virtual void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) = 0;

        // draw a retained immutable path under a transform, which replays without rebuilding geometry (§6.4, AD-06)
        virtual void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) = 0;

        // replay a retained picture's commands in order under a transform (§6.4, AD-06)
        virtual void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) = 0;

        // draw a retained immutable image into a destination rectangle (§6.4, AD-06)
        virtual void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) = 0;
    };

    // the surface a window presents into, which owns one drawing context per frame (§6.2)
    class RenderSurface
    {
    public:
        virtual ~RenderSurface() = default;

        // report whether the surface is ready to accept a frame
        [[nodiscard]] virtual bool valid() const = 0;

        // hand out the metrics the surface presents at
        [[nodiscard]] virtual const WindowMetrics& metrics() const = 0;

        // resize the surface, which a window resize requires (§6.2)
        virtual void resize(const WindowMetrics& metrics) = 0;

        // begin a frame and hand out the context that draws it, keeping the frame state the end resolves (§6.1, AD-18)
        virtual RenderContext& begin_frame() = 0;

        // end the frame and present the frame state the begin kept, exactly once per begun frame (§6.1, §6.3)
        virtual void end_frame() = 0;
    };
} // namespace albor
