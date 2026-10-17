#pragma once

#include <span>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/render-skia/skia_render_context.h"
#include "render/render-software/software_glyph_atlas.h"
#include "render/render_context.h"

class SkCanvas;

namespace albor::software
{
    // the software context, which draws text through its own glyph atlas and everything else through skia (§6.3)
    class SoftwareRenderContext final : public RenderContext
    {
    public:
        // wrap a raster canvas the reference draws into, which the atlas composites its quads onto (§6.3, AD-26)
        SoftwareRenderContext(SkCanvas* canvas, float scale_factor);

        // override: draw a shaped run from the atlas, which is the reference's own rasterizer (§6.3, AD-26)
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;

        // the remaining drawing delegates to the shared skia context, so rects and paths cannot drift (§6.3)
        void save() override;
        void restore() override;
        void set_transform(const Transform& transform) override;
        [[nodiscard]] const Transform& transform() const override;
        void clip_rect(const Rect& rect) override;
        void clip_rounded_rect(const Rect& rect, float radius) override;
        [[nodiscard]] const Rect& clip() const override;
        void set_opacity(float opacity) override;
        [[nodiscard]] float opacity() const override;
        void draw_rect(const Rect& rect, const Paint& paint) override;
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;
        void draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) override;
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

    private:
        // the canvas the atlas quads are composited onto, which the surface owns (§6.3)
        SkCanvas* m_canvas = nullptr;

        // the shared skia context every non text draw delegates to, so the two backends stay one interpretation (§6.3)
        skia::SkiaRenderContext m_inner;

        // the reference's own glyph atlas, which rasterizes from the carried bytes rather than skia (§6.3, AD-34)
        SoftwareGlyphAtlas m_atlas;

        // the device scale the canvas maps logical to, which the atlas rasterizes at so hidpi text stays sharp (§6.3)
        float m_scale = 1.0f;
    };
} // namespace albor::software
