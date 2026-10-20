#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "render/path_resource.h"
#include "render/render_context.h"

// the skia canvas a render context draws into, so that no skia header reaches this file (§3.5, AD-05)
class SkCanvas;

// the skia matrix and path the context builds, so that no skia header reaches this file (§3.5, AD-05)
class SkMatrix;
class SkPath;

namespace albor::skia
{
    // the render context that wraps one skia canvas for the duration of a frame (§6.3)
    class SkiaRenderContext final : public RenderContext
    {
    public:
        // wrap the canvas of the frame being drawn, scaled from logical points onto the physical drawable (§6.3, AD-27)
        SkiaRenderContext(SkCanvas* canvas, float device_scale);

        // override: push the current transform and clip
        void save() override;

        // override: pop the transform and clip the matching save pushed
        void restore() override;

        // override: replace the current transform
        void set_transform(const Transform& transform) override;

        // override: read back the current transform
        [[nodiscard]] const Transform& transform() const override;

        // override: intersect the clip with an axis aligned rectangle
        void clip_rect(const Rect& rect) override;

        // override: intersect the clip with a rounded rectangle
        void clip_rounded_rect(const Rect& rect, float radius) override;

        // override: read back the current clip
        [[nodiscard]] const Rect& clip() const override;

        // override: set the opacity the later draws are dimmed by
        void set_opacity(float opacity) override;

        // override: read back the current opacity
        [[nodiscard]] float opacity() const override;

        // override: begin a layer the later draws composite into, which restores this state at the matching pop
        void push_layer(float opacity) override;

        // override: end the layer the matching push began
        void pop_layer() override;

        // override: draw a filled and stroked rectangle
        void draw_rect(const Rect& rect, const Paint& paint) override;

        // override: draw a filled and stroked rounded rectangle
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;

        // override: draw a polyline joined from vertex to vertex, which is what a control glyph needs (§6.2)
        void draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) override;

        // override: blur a shape offscreen and composite the cast under it
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;

        // override: draw a text run at its baseline origin
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;

        // override: draw a retained immutable line batch under a transform
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;

        // override: draw a retained immutable path under a transform
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;

        // override: replay a retained picture's commands in order under a transform
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;

        // override: draw a retained immutable image into a destination rectangle
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

        // hand out how deep the state stack is, which a test asserts against the saves it made
        [[nodiscard]] std::size_t depth() const;

    private:
        // the transform and clip one save pushed
        struct State
        {
            // the transform at the time of the save
            Transform transform;

            // the clip at the time of the save
            Rect clip;

            // the opacity at the time of the save
            float opacity = 1.0f;
        };

        // convert an albor colour into the skia colour the canvas expects, with the current opacity folded into the alpha
        [[nodiscard]] unsigned int to_skia_color(const Color& color) const;

        // fill and stroke a shape, which is the paint handling both rectangles share
        void paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius);

        // convert an albor transform into the matrix the canvas concat expects, which is the transform without the base scale (§6.3)
        [[nodiscard]] static SkMatrix to_skia_matrix(const Transform& transform);

        // build the skia path of a command stream, which the batch and the path replay share
        [[nodiscard]] static SkPath build_path(std::span<const PathVerb> verbs, std::span<const Point> points);

        // the canvas of the frame being drawn, which is null when no frame began
        SkCanvas* m_canvas;

        // the scale from logical points onto the physical drawable, which the frames present at (§6.3, AD-27)
        float m_device_scale;

        // the current transform
        Transform m_transform;

        // the current clip
        Rect m_clip;

        // the opacity the later draws are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops
        std::vector<State> m_states;

        // how many layers are open, so a mismatched pop cannot unwind a save that was not one (§6.2)
        std::size_t m_layers = 0;
    };
} // namespace albor::skia
