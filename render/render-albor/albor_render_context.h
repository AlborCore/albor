#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/clip_state.h"
#include "render/path_resource.h"
#include "render/render-albor/albor_batch.h"
#include "render/render-albor/albor_contour.h"
#include "render/render-albor/albor_glyph_cache.h"
#include "render/render_context.h"

namespace albor::render
{
    // the render context that turns the drawing hal into the portable batches a gpu backend uploads (§6.2, §6.3)
    class AlborRenderContext final : public RenderContext
    {
    public:
        // wrap the batch of the frame being built, which draws logical points onto the physical raster (AD-27)
        AlborRenderContext(FrameBatch& batch, const Rect& logical_extent, float device_scale, GlyphPathCache* glyph_paths = nullptr);

        AlborRenderContext(const AlborRenderContext&) = delete;
        AlborRenderContext& operator=(const AlborRenderContext&) = delete;
        AlborRenderContext(AlborRenderContext&&) = delete;
        AlborRenderContext& operator=(AlborRenderContext&&) = delete;
        ~AlborRenderContext() override = default;

        void save() override;
        void restore() override;

        void set_transform(const Transform& transform) override;
        [[nodiscard]] const Transform& transform() const override;

        void clip_rect(const Rect& rect) override;
        void clip_rounded_rect(const Rect& rect, float radius) override;
        [[nodiscard]] const Rect& clip() const override;

        void set_opacity(float opacity) override;
        [[nodiscard]] float opacity() const override;

        void push_layer(float opacity) override;
        void pop_layer() override;

        void draw_rect(const Rect& rect, const Paint& paint) override;
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;
        void draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) override;
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

        // hand out how deep the state stack is, which a test asserts against the saves it made (§6.2)
        [[nodiscard]] std::size_t depth() const;

        // hand out the scissor the next draw would carry, which a test asserts against (§6.2)
        [[nodiscard]] BatchScissor scissor() const;

        // hand out the device transform the next draw would land under, which a test asserts against (§6.2, AD-27)
        [[nodiscard]] Transform device_transform() const;

    private:
        // the transform, clip and opacity one save pushed (§6.2)
        struct State
        {
            // the scene space transform the draw lands under
            Transform transform;

            // the scene space clip the draw is cut by, radius included
            ClipState clip;

            // the device transform the clip was pushed under, which the scissor freezes (§6.2)
            Transform clip_device;

            // the opacity the draw is dimmed by
            float opacity = 1.0f;
        };

        // report whether a draw under the current transform can put any ink down at all (§6.2)
        [[nodiscard]] bool drawable() const;

        // map a scene space contour into the device space the expansion rasterizes in (§6.3, AD-27)
        void to_device(Contour& contour) const;

        // scale a logical length by the average device scale, which a stroke half width needs (§6.3, AD-27)
        [[nodiscard]] float to_device_length(float length) const;

        // emit the fill of a scene space contour under a colour, a blend and a draw record (§6.2)
        void emit_fill(Contour contour, const Color& color, std::uint64_t texture, CompositionMode composition);

        // emit the stroke of a scene space contour under a colour, a blend and a draw record (§6.2)
        void emit_stroke(Contour contour, const Color& color, float width, Cap cap, Join join, CompositionMode composition);

        // expand the stroke of a scene space contour without recording it, which a batch coalesces (§6.4)
        [[nodiscard]] StrokeEmission stroke_geometry(Contour& contour, float width, Cap cap, Join join);

        // emit a shape that both fills and strokes, which the rectangle primitives share (§6.2)
        void emit_shape(Contour contour, const Paint& paint, Cap cap, Join join);

        // emit an image as the two triangles its destination rectangle spans (§6.4)
        void emit_image(const Rect& destination, const Color& color, std::uint64_t texture, CompositionMode composition);

        // record one draw over the index range an expansion appended (§6.3)
        void record(EmittedRange range, CoverageKind coverage, float ramp, const Color& color, std::uint64_t texture, bool convex, CompositionMode composition);

        // the batch the frame being built accumulates into (§6.3)
        FrameBatch& m_batch;

        // the logical extent the frame covers, which an unclipped draw spans (AD-27)
        Rect m_extent;

        // the ratio between the physical and the logical extents (AD-27)
        float m_device_scale;

        // the glyph paths the surface keeps across frames, which this context borrows so a glyph walks once (§6.4, AD-06)
        GlyphPathCache* m_glyph_paths;

        // the scene space transform the later draws land under (§6.2)
        Transform m_transform;

        // the scene space clip the later draws are cut by, radius included (§6.2)
        ClipState m_clip;

        // the device transform the clip was pushed under, which the scissor freezes (§6.2)
        Transform m_clip_device;

        // the opacity the later draws are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> m_states;

        // the batch layers that are open, which a pop closes by index rather than by search (§6.2)
        std::vector<std::size_t> m_layers;
    };
} // namespace albor::render
