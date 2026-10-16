#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "render/path_resource.h"
#include "render/render_context.h"

// the blend2d raster context a render context draws into, so that no blend2d header reaches this file (§3.5, AD-05)
class BLContext;

// the blend2d path the context builds, so that no blend2d header reaches this file (§3.5, AD-05)
class BLPath;

// the blend2d font the context resolves a run with, so that no blend2d header reaches this file (§3.5, AD-05)
class BLFont;

// the blend2d matrix the context transforms with, so that no blend2d header reaches this file (§3.5, AD-05)
struct BLMatrix2D;

// the blend2d colour the context fills and strokes with, so that no blend2d header reaches this file (§3.5, AD-05)
struct BLRgba32;

namespace albor::blend2d
{
    // the render context that wraps one blend2d raster context for the duration of a frame (§6.3)
    class Blend2DRenderContext final : public RenderContext
    {
    public:
        // wrap the raster context of the frame, scaled from logical points onto the physical pixels (§6.3, AD-27)
        Blend2DRenderContext(BLContext* context, float device_scale, Rect full_clip);

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
        // the transform, clip and opacity one save pushed
        struct State
        {
            // the transform at the time of the save
            Transform transform;

            // the clip at the time of the save
            Rect clip;

            // the opacity at the time of the save
            float opacity = 1.0f;
        };

        // convert an albor colour into the blend2d colour the raster expects, with the opacity folded into alpha
        [[nodiscard]] BLRgba32 to_blend_color(const Color& color) const;

        // fill and stroke a shape, which is the paint handling both rectangles share
        void paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius);

        // convert an albor transform into the matrix the raster context concats, without the base scale (§6.3)
        [[nodiscard]] static BLMatrix2D to_blend_matrix(const Transform& transform);

        // build the blend2d path of a command stream, which the batch and the path replay share
        [[nodiscard]] static BLPath build_path(std::span<const PathVerb> verbs, std::span<const Point> points);

        // resolve the run's font from the registered bytes, which is empty when no font of that hash is registered (§9.2)
        [[nodiscard]] BLFont resolve_font(std::uint64_t font_hash, float font_size);

        // the raster context of the frame being drawn, which is null when no frame began
        BLContext* m_context;

        // the scale from logical points onto the physical pixels, which the frames present at (§6.3, AD-27)
        float m_device_scale;

        // the current transform
        Transform m_transform;

        // the current clip
        Rect m_clip;

        // the opacity the later draws are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops
        std::vector<State> m_states;
    };
} // namespace albor::blend2d
