#pragma once

#include <vector>

#include "render/render_context.h"

// the skia canvas a render context draws into, so that no skia header reaches this file (§3.5, AD-05)
class SkCanvas;

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

        // override: draw a filled and stroked rectangle
        void draw_rect(const Rect& rect, const Paint& paint) override;

        // override: draw a filled and stroked rounded rectangle
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;

        // override: draw a text run at its baseline origin
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;

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
        };

        // convert an albor colour into the skia colour the canvas expects
        [[nodiscard]] static unsigned int to_skia_color(const Color& color);

        // fill and stroke a shape, which is the paint handling both rectangles share
        void paint_shape(const Paint& paint, bool rounded, const Rect& rect, float radius);

        // the canvas of the frame being drawn, which is null when no frame began
        SkCanvas* m_canvas;

        // the scale from logical points onto the physical drawable, which the frames present at (§6.3, AD-27)
        float m_device_scale;

        // the current transform
        Transform m_transform;

        // the current clip
        Rect m_clip;

        // the pushed states, which a restore pops
        std::vector<State> m_states;
    };
} // namespace albor::skia
