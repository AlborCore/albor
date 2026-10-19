#include "ui/scene_builder.h"

#include <algorithm>

#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/picture_resource.h"

namespace albor
{
    SceneRecorder::SceneRecorder(WindowMetrics metrics) :
        m_metrics(metrics),
        // the clip starts as the whole logical surface, which is what keeps an unclipped tree from culling itself (§6.2)
        m_clip(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size)) {}

    void SceneRecorder::save() {
        // the current state is pushed before it can be replaced, so a restore has something to return to (§6.2)
        m_states.push_back(State{.transform = m_transform, .clip = m_clip, .opacity = m_opacity});
    }

    void SceneRecorder::restore() {
        // a restore with nothing pushed would pop below the empty state stack (§6.2)
        if (m_states.empty())
            return;
        const State& last_state = m_states.back();
        // the state returns to what the matching save pushed (§6.2)
        m_transform = last_state.transform;
        m_clip = last_state.clip;
        m_opacity = last_state.opacity;
        m_states.pop_back();
    }

    void SceneRecorder::set_transform(const Transform& transform) {
        // the recorder stores the transform so it can map every later primitive into scene coordinates (§6.1)
        m_transform = transform;
    }

    const Transform& SceneRecorder::transform() const {
        // the transform is read back by a node that composes on top of it (§6.2)
        return m_transform;
    }

    void SceneRecorder::clip_rect(const Rect& rect) {
        // the clip narrows rather than replaces, because a nested clip may only ever cut more away (§6.2)
        clip_to_rect(m_clip, rect);
    }

    void SceneRecorder::clip_rounded_rect(const Rect& rect, float radius) {
        // a rounded clip keeps its radius, which the replay scissors by (§6.2)
        clip_to_rounded_rect(m_clip, rect, radius);
    }

    const Rect& SceneRecorder::clip() const {
        // the clip is read back by a node that tests visibility against it (§6.2)
        return m_clip.rect;
    }

    void SceneRecorder::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because a binding computed from data can land outside it (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float SceneRecorder::opacity() const {
        // the opacity is read back by a node that composes its own on top of it (§6.1)
        return m_opacity;
    }

    void SceneRecorder::draw_rect(const Rect& rect, const Paint& paint) {
        // the rectangle is mapped into scene coordinates now, because the render thread cannot ask where (§6.1, AD-18)
        record(DrawCommand::rectangle(to_scene(rect), paint));
    }

    void SceneRecorder::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the radii travel with the geometry, which is what the bucketed scene used to lose (§6.2)
        record(DrawCommand::rounded_rectangle(to_scene(rect), radius, paint));
    }

    void SceneRecorder::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // every vertex is mapped into scene coordinates now, because the render thread cannot ask where (§6.1, AD-18)
        std::vector<Point> mapped;
        mapped.reserve(points.size());
        for (const Point& point : points)
            mapped.push_back(to_scene(point));
        // the polyline records as the vertex run the replay joins (§6.2)
        record(DrawCommand::polyline(mapped, stroke));
    }

    void SceneRecorder::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a cast with no colour or no shape puts no ink down, so it is never recorded (§6.2)
        if (!shadow.casts() || rect.empty())
            return;
        // the shape is mapped into scene coordinates now, because the render thread cannot ask where (§6.1, AD-18)
        record(DrawCommand::shadow(to_scene(rect), radius, shadow));
    }

    void SceneRecorder::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // an embedded run the platform renderer cannot hold expands into outlines instead (§15, AD-34)
        if (layout.run != nullptr && layout.run->font_hash() != 0 && layout.run->shape_source() == ShapeSource::Embedded) {
            // the expansion is the AD-34 path, and it is what makes embedded text reach a surface (§6.4)
            record_embedded_run(layout, origin, paint);
            return;
        }
        // a platform run goes through the text command at the mapped origin, drawn by the typeface a renderer holds (§9.1)
        record(DrawCommand::text(layout, to_scene(origin), paint));
    }

    void SceneRecorder::record_embedded_run(const TextLayout& layout, Point origin, const Paint& paint) {
        // a recorder with no resolver cannot build an outline, so the run draws nothing rather than silently (§6.4)
        if (!m_outline_resolver)
            return;
        // every line of the run places its own baseline, which the glyphs hang from (§9.1)
        for (const ShapedRun::Line& line : layout.run->lines()) {
            // the baseline is the run origin under the line's own origin, which is where every glyph sits (§9.1)
            const Point baseline{origin.x + line.origin.x, origin.y + line.origin.y};
            // every glyph becomes one outline command at the place the shaped run gave it (§6.4, AD-34)
            for (const ShapedGlyph& glyph : line.glyphs) {
                // the key is everything that changes the shape, so a cache hit is provably the same outline (§6.4)
                const GlyphOutlineKey key{.font_hash = layout.run->font_hash(), .glyph_id = glyph.id, .font_size = layout.run->font_size()};
                // the outline comes from the borrowed cache, or straight from the resolver without one (§6.4, AD-06)
                const std::shared_ptr<const GlyphOutline> outline = m_outline_cache != nullptr ? m_outline_cache->lookup(key, m_outline_resolver) : m_outline_resolver(key);
                // a glyph the build cannot outline draws nothing, which is a refusal rather than a blank (§6.4)
                if (outline == nullptr || outline->empty())
                    continue;
                // the glyph sits at its own offset from the baseline, which the run measured (§9.1)
                const Point position{baseline.x + glyph.position.x, baseline.y + glyph.position.y};
                // the outline is recorded as the command stage two replays (§6.4, AD-34)
                glyph_outline(outline, position, paint);
                // the command is counted, which is what a test asserts on (§6.4)
                m_outline_commands++;
            }
        }
    }

    void SceneRecorder::set_outline_resolver(GlyphOutlineResolver resolver) {
        // the resolver belongs to the recorder, which is what a composition root installs (§6.4, §11.3)
        m_outline_resolver = std::move(resolver);
    }

    void SceneRecorder::set_outline_cache(GlyphOutlineCache* cache) {
        // the caller keeps the cache alive for this recorder's life, which the builder's own member does (§6.4, AD-06)
        m_outline_cache = cache;
    }

    std::size_t SceneRecorder::outline_command_count() const {
        // the count is what a test asserts on, because the commands are otherwise invisible (§6.4)
        return m_outline_commands;
    }

    void SceneRecorder::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // the transform is composed once, so the retained draw replays in constant time (§6.4, AD-06)
        record(DrawCommand::line_batch(batch, transform.then(m_transform), stroke));
    }

    void SceneRecorder::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // the transform is composed once, so the retained draw replays in constant time (§6.4, AD-06)
        record(DrawCommand::path(path, transform.then(m_transform), paint));
    }

    void SceneRecorder::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // the transform is composed once, so the retained draw replays in constant time (§6.4, AD-06)
        record(DrawCommand::picture(picture, transform.then(m_transform)));
    }

    void SceneRecorder::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // the destination is mapped into scene space now, like a recorded rectangle (§6.4)
        record(DrawCommand::image(image, to_scene(destination), paint));
    }

    void SceneRecorder::glyph_outline(const std::shared_ptr<const GlyphOutline>& outline, Point position, const Paint& paint) {
        // the pen and the transform travel with the outline, so a scaled subtree scales its glyphs (§6.4, §6.1)
        record(DrawCommand::glyph_outline(outline, position, paint, m_transform));
    }

    void SceneRecorder::set_background(Color background) {
        // the background is the clear colour of the whole frame (§6.1)
        m_background = background;
    }

    Rect SceneRecorder::to_scene(const Rect& rect) const {
        // the origin maps through the transform, so a nested node lands where its parent placed it (§6.1)
        const Point mapped = to_scene(Point{.x = rect.x, .y = rect.y});
        // the extent scales with the transform, so a scaled node draws a scaled rectangle (§6.1)
        return Rect{.x = mapped.x, .y = mapped.y, .width = rect.width * m_transform.scale_x, .height = rect.height * m_transform.scale_y};
    }

    Point SceneRecorder::to_scene(const Point& point) const {
        // the mapping is the same affine transform the backend would apply, so stage one predicts stage two (§6.1)
        return m_transform.apply(point);
    }

    std::shared_ptr<const FrameScene> SceneRecorder::scene() const {
        // the recorded commands build the scene, which is what stage two replays (§6.1)
        std::shared_ptr<FrameScene> scene = std::make_shared<FrameScene>(m_metrics, m_background);
        // the commands travel into the scene in paint order, because stage two replays them as recorded (§6.1, AD-18)
        for (const DrawCommand& command : m_commands)
            scene->append_command(command);
        return scene;
    }

    void SceneRecorder::record(DrawCommand command) {
        // the clip is stamped first, because a command carries the clip it was recorded under (§6.2)
        command.set_clip(m_clip.rect);
        // the radius rides with the clip, which a rounded clip scissors by (§6.2)
        command.set_clip_radius(m_clip.radius);
        // the cull is decided on the authored paint, because a draw the author made is a draw even at no opacity (§6.1)
        if (command.draws_nothing())
            return;
        // a fully opaque context leaves every paint alone, which is the common case (§6.1)
        if (m_opacity < 1.0f)
            command.scale_alpha(m_opacity);
        // the scene holds the recorded commands in paint order, which is what stage two replays as recorded (§6.1)
        m_commands.push_back(std::move(command));
    }

    std::size_t SceneRecorder::depth() const {
        // the depth is the unbalanced save count, which a well formed node tree leaves at zero (§6.2)
        return m_states.size();
    }

    SceneBuilder::SceneBuilder() = default;

    void SceneBuilder::number(UiNode& root) {
        // the in tree identifier is a correlation hint for live reload, never an automation key (AD-11, §18.1)
        root.set_node_id(m_next_node);
        m_last_node = m_next_node;
        m_next_node++;
        for (const auto& child : root.children())
            number(*child);
    }

    std::shared_ptr<const FrameScene> SceneBuilder::build(UiNode& root, WindowMetrics metrics, Color background) {
        m_visited = 0;
        m_drawn = 0;
        m_laid_out = 0;
        m_next_node = 1;
        m_last_node = 0;
        // the numbering runs first, so every node carries its correlation hint before it draws (§18.1)
        number(root);
        // a window resize changes what the tree has to fit into, so it lays out even when nothing invalidated (§8.2)
        const bool resized = metrics != m_built_metrics;
        // the metrics of this build are remembered, so the next one can tell whether the window moved (§8.2)
        m_built_metrics = metrics;
        // the measure pass runs when something asked for an extent, and a resize is such an ask (§8.2)
        const bool measure = root.measure_dirty() || resized;
        // the arrange pass runs when the measure ran or something only asked to move, because both place (§8.2)
        const bool arrange = measure || root.arrange_dirty();
        // the measure settles what every node asks for, and it travels top down from the window (§8.2)
        if (measure) {
            // the root is measured against the window, which is the whole of the available space (§8.2)
            Size measured{};
            root.measure(SizeConstraints::tight(metrics.logical_size), measured);
        }
        // the arrange places every node, and it reuses the last measure wherever the extent did not change (§8.2)
        if (arrange) {
            // the root is arranged to the full window, because a ui tree covers its surface (§8.2)
            root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        }
        SceneRecorder recorder(metrics);
        // the resolver is handed on, because an embedded run expands into outlines during this recording (§6.4, AD-34)
        recorder.set_outline_resolver(m_outline_resolver);
        // the outline cache is handed on as well, because the recorder itself is built per frame (§6.4, AD-06)
        recorder.set_outline_cache(&m_outline_cache);
        recorder.set_background(background);
        recorder.save();
        // an invisible tree draws nothing, so the scene carries only the background (§6.1)
        const bool draws = root.visible();
        if (draws)
            root.render(recorder);
        recorder.restore();
        // one walk counts the phases of the build, because the layout and the record pass both touched the subtree (§6.1, §8.2)
        root.visit([this, draws](const UiNode& node) {
            // every node of the subtree was numbered and walked (§6.1)
            m_visited++;
            // a node the layout gave area is placed, while a zero extent node occupies no cell of the frame (§8.2)
            if (!node.bounds().empty())
                m_laid_out++;
            // only a visible node reaches the renderer, because stage one visits the visible tree (§6.1)
            if (draws && node.visible())
                m_drawn++;
        });
        // the dirty flags clear only after a successful build, so a failed build keeps them (§6.1)
        root.clear_dirty();
        return recorder.scene();
    }

    void SceneBuilder::set_outline_resolver(GlyphOutlineResolver resolver) {
        // the resolver belongs to the builder, which hands it to each recorder it creates (§6.4, §11.3)
        m_outline_resolver = std::move(resolver);
    }

    std::size_t SceneBuilder::visited_count() const { return m_visited; }

    std::size_t SceneBuilder::drawn_count() const { return m_drawn; }

    std::size_t SceneBuilder::laid_out_count() const { return m_laid_out; }

    NodeId SceneBuilder::last_node_id() const { return m_last_node; }
} // namespace albor
