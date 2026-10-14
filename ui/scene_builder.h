#pragma once

#include <memory>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/draw_command.h"
#include "render/frame_scene.h"
#include "render/glyph_outline.h"
#include "render/render_context.h"
#include "ui/ui_node.h"
#include "window/window_metrics.h"
namespace albor
{
    // the render context that records stage one commands into an immutable scene, so no backend type appears here (§3.5, AD-05)
    class SceneRecorder final : public RenderContext
    {
    public:
        // build a recorder over the metrics the scene will be presented at (§6.1)
        explicit SceneRecorder(WindowMetrics metrics);

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

        // override: record a filled and stroked rectangle
        void draw_rect(const Rect& rect, const Paint& paint) override;

        // override: record a filled and stroked rounded rectangle
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;

        // override: record a polyline joined from vertex to vertex
        void draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) override;

        // override: record the shadow a shape casts
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;

        // override: record a text run at its baseline origin
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;

        // override: record a retained line batch under a transform
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;

        // override: record a retained path under a transform
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;

        // override: record a retained picture under a transform
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;

        // override: record a retained image into a destination rectangle
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

        // record one embedded glyph as its outline, which is not a context primitive because the command carries it
        void glyph_outline(const std::shared_ptr<const GlyphOutline>& outline, Point position, const Paint& paint);

        // install the resolver that turns an embedded run into outline commands, which a composition root does (§6.4)
        void set_outline_resolver(GlyphOutlineResolver resolver);

        // report how many outline commands this recorder emitted, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t outline_command_count() const;

        // set the background the scene is cleared with (§6.1)
        void set_background(Color background);

        // freeze the recorded commands into the immutable scene the render thread consumes (§6.1, AD-18)
        [[nodiscard]] std::shared_ptr<const FrameScene> scene() const;

        // hand out how many save and restore pairs are balanced, which a test asserts on (§6.2)
        [[nodiscard]] std::size_t depth() const;

    private:
        // record an embedded run as one outline command per glyph, which is how AD-34 text draws (§6.4, AD-34)
        void record_embedded_run(const TextLayout& layout, Point origin, const Paint& paint);

        // the resolver that builds a glyph outline, which is empty until a composition root installs one (§6.4)
        GlyphOutlineResolver m_outline_resolver;

        // the outlines this recorder already built, which a frame would otherwise rebuild per glyph (§6.4)
        GlyphOutlineCache m_outline_cache;

        // the outline commands this recorder emitted, which a test asserts on (§6.4)
        std::size_t m_outline_commands = 0;

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

        // map a local rectangle into scene coordinates through the current transform (§6.1)
        [[nodiscard]] Rect to_scene(const Rect& rect) const;

        // map a local point into scene coordinates through the current transform (§6.1)
        [[nodiscard]] Point to_scene(const Point& point) const;

        // record a command after stamping the clip it was drawn under (§6.2)
        void record(DrawCommand command);

        // the metrics the scene is built for (§6.1)
        WindowMetrics m_metrics;

        // the background the surface is cleared with (§6.1)
        Color m_background;

        // the current transform
        Transform m_transform;

        // the current clip
        Rect m_clip;

        // the opacity the recorded commands are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> m_states;

        // the recorded commands in paint order, which the scene hands to stage two (§6.1, AD-18)
        std::vector<DrawCommand> m_commands;
    };

    // stage one of the frame pipeline, which lays out and records the ui tree into an immutable scene (§6.1, AD-18)
    class SceneBuilder
    {
    public:
        // build a builder that numbers the nodes it walks (§18.1)
        SceneBuilder();

        // lay out and record the tree for the given metrics, which returns the scene stage two consumes (§6.1)
        [[nodiscard]] std::shared_ptr<const FrameScene> build(UiNode& root, WindowMetrics metrics, Color background);

        // install the resolver embedded text draws through, which a composition root does once (§6.4, AD-34)
        void set_outline_resolver(GlyphOutlineResolver resolver);

        // hand out how many nodes the last build visited, which a test asserts on (§6.1)
        [[nodiscard]] std::size_t visited_count() const;

        // hand out how many nodes the last build drew, meaning the visible ones that reached the renderer, which a test asserts on (§6.1)
        [[nodiscard]] std::size_t drawn_count() const;

        // hand out how many nodes the last build laid out, meaning the ones the layout gave area, which a test asserts on (§8.2)
        [[nodiscard]] std::size_t laid_out_count() const;

        // hand out the identifier the last node was numbered with (§18.1)
        [[nodiscard]] NodeId last_node_id() const;

    private:
        // number every node of the tree, which gives the in tree correlation hint (§18.1)
        void number(UiNode& root);

        // the resolver embedded text draws through, which a builder without one has none of (§6.4, AD-34)
        GlyphOutlineResolver m_outline_resolver;

        // how many nodes the last build visited, which a test asserts on (§6.1)
        std::size_t m_visited = 0;

        // how many nodes the last build drew, which are the visible ones (§6.1)
        std::size_t m_drawn = 0;

        // how many nodes the last build gave area, which are the ones placed in the frame (§8.2)
        std::size_t m_laid_out = 0;

        // the identifier the next numbered node receives (§18.1)
        NodeId m_next_node = 1;

        // the identifier the last numbered node received (§18.1)
        NodeId m_last_node = 0;

        // the metrics the last build laid the tree out for, which a change of lays it out again (§8.2)
        WindowMetrics m_built_metrics;
    };
} // namespace albor
