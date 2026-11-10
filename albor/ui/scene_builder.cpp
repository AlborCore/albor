#include "albor/ui/scene_builder.h"

#include <algorithm>

#include "albor/render/image_resource.h"
#include "albor/render/line_batch.h"
#include "albor/render/path_resource.h"
#include "albor/render/picture_resource.h"

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

    bool SceneRecorder::has_damage_filter() const {
        // an empty filter culls nothing, so the recorder answers as an unfiltered one (§6.1)
        return m_damage_filter.has_value() && !m_damage_filter->empty();
    }

    Rect SceneRecorder::damage_filter() const {
        // a recorder with no filter reports an empty region, which a culling node never reads (§6.1)
        return m_damage_filter.value_or(Rect{});
    }

    void SceneRecorder::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because a binding computed from data can land outside it (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float SceneRecorder::opacity() const {
        // the opacity is read back by a node that composes its own on top of it (§6.1)
        return m_opacity;
    }

    void SceneRecorder::push_layer(float opacity) {
        // the layer begins at the state the recorder stands at, which its pop returns to (§6.2)
        save();
        // the marker carries the clamped group opacity, which the replay composites once (§6.2)
        record(DrawCommand::layer_push(opacity));
        // the open count tells a later pop that a layer is waiting for it (§6.2)
        m_layers++;
    }

    void SceneRecorder::pop_layer() {
        // a pop with no layer open is a no operation, because the pair is the caller's contract (§6.2)
        if (m_layers == 0)
            return;
        // the layer is no longer open, which the next pop reads (§6.2)
        m_layers--;
        // the state the push saved becomes current again, which balances its save (§6.2)
        restore();
        // the marker closes the group under the clip the push recorded, so the pair carries one clip (§6.2)
        record(DrawCommand::layer_pop());
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

    void SceneRecorder::set_damage_rect(std::optional<Rect> damage) {
        // the damage region is the part stage two may redraw, or nothing for a full frame (§6.1)
        m_damage = std::move(damage);
    }

    void SceneRecorder::set_damage_filter(std::optional<Rect> damage) {
        // the filter is the part of the frame stage one keeps, or nothing to keep all of it (§6.1)
        m_damage_filter = std::move(damage);
    }

    bool SceneRecorder::keeps(const DrawCommand& command) const {
        // a recorder with no filter keeps every command, which is the full frame case (§6.1)
        if (!has_damage_filter())
            return true;
        // a layer marker carries no geometry, so it stays paired with its sibling either way (§6.2)
        if (command.kind() == DrawCommandKind::LayerPush || command.kind() == DrawCommandKind::LayerPop)
            return true;
        // a command that draws nothing contributes no pixels, so it is dropped with the rest (§6.1)
        if (command.draws_nothing())
            return false;
        // a command whose damaged area misses the region cannot paint inside it (§6.1)
        return !command.damage_bounds().intersect(*m_damage_filter).empty();
    }

    Rect SceneRecorder::to_scene(const Rect& rect) const {
        // the whole transform maps both corners, so a scaled, rotated or sheared record lands where stage two draws (§6.1)
        return m_transform.map(rect);
    }

    Point SceneRecorder::to_scene(const Point& point) const {
        // the mapping is the same affine transform the backend would apply, so stage one predicts stage two (§6.1)
        return m_transform.apply(point);
    }

    std::shared_ptr<const FrameScene> SceneRecorder::scene() const {
        // a scene recorded against a filter omits whatever lay outside it, which needs a retained target (§6.1)
        const bool culls_commands = has_damage_filter();
        // the scene carries the damage, the background and whether its command list is whole (§6.1)
        std::shared_ptr<FrameScene> scene = std::make_shared<FrameScene>(m_metrics, m_background, m_damage, culls_commands);
        // the commands travel into the scene in paint order, because stage two replays them as recorded (§6.1, AD-18)
        for (const DrawCommand& command : m_commands) {
            // a command outside the damage region is left out, which is what stage one saves (§6.1)
            if (keeps(command))
                scene->append_command(command);
        }
        // a layer the caller left open is closed at the seal, so a malformed tree still composites once (§6.2)
        for (std::size_t open = m_layers; open > 0; --open) {
            // the synthesized pop carries the clip the recorder ended under, which the replay ignores for markers (§6.2)
            DrawCommand close = DrawCommand::layer_pop();
            close.set_clip(m_clip.rect);
            close.set_clip_radius(m_clip.radius);
            scene->append_command(std::move(close));
        }
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
        // a culling recorder computes the damage bound once, because stage two reads the same value off the scene (§6.1)
        if (has_damage_filter() && command.kind() != DrawCommandKind::LayerPush && command.kind() != DrawCommandKind::LayerPop)
            command.cache_damage_bounds();
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

    std::shared_ptr<const FrameScene> SceneBuilder::build(UiNode& root, WindowMetrics metrics, Color background, bool cull_commands) {
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
        // the arrange places every node, reusing the last measure when the extent holds (§8.2)
        if (arrange) {
            // the root is arranged to the full window, because a ui tree covers its surface (§8.2)
            root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        }
        // a measure pass can resize any node, while an arrange pass records the old and new extents (§6.1, §8.2)
        const bool full_frame = resized || measure;
        // the damage is read after layout, because a moved node reports where it now draws (§8.2)
        std::optional<Rect> damage;
        if (!full_frame) {
            // the root carries the union of every invalidation since the last frame (§6.1)
            damage = root.damage_region();
            // one logical pixel covers the antialias fringe a painted edge can leave (§6.1)
            if (damage)
                damage = damage->inflated(1.0f);
            // a node outside the surface contributes no pixels, so its damage is clipped to the frame (§6.1)
            if (damage)
                damage = damage->intersect(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
            // an empty region falls back to the full frame rather than lying about it (§6.1)
            if (damage && damage->empty())
                damage.reset();
            // a region covering the whole surface excludes nothing, so the frame is recorded whole (§6.1)
            if (damage && damage->width >= metrics.logical_size.width && damage->height >= metrics.logical_size.height)
                damage.reset();
        }
        // the damage is spent before recording, so an invalidation raised while drawing stays for the next frame (§6.1)
        root.clear_damage_region();
        // a culled scene needs a surface that keeps its target, which the pipeline reports (§6.1)
        const bool culls = cull_commands && damage.has_value();
        SceneRecorder recorder(metrics);
        // the resolver is handed on, because an embedded run expands into outlines during this recording (§6.4, AD-34)
        recorder.set_outline_resolver(m_outline_resolver);
        // the outline cache is handed on as well, because the recorder itself is built per frame (§6.4, AD-06)
        recorder.set_outline_cache(&m_outline_cache);
        recorder.set_background(background);
        // the damage region is what stage two redraws when the list is whole, which any surface receives (§6.1)
        recorder.set_damage_rect(damage);
        // only a retaining surface may receive a culled list, which is the stage one half of the damage pipeline (§6.1)
        recorder.set_damage_filter(culls ? damage : std::nullopt);
        recorder.save();
        // an invisible tree draws nothing, so the scene carries only the background (§6.1)
        const bool draws = root.visible();
        if (draws)
            root.render(recorder);
        recorder.restore();
        // one walk counts the phases of the build, the layout and the record pass both touched the subtree (§6.1, §8.2)
        root.visit([this, draws, &damage](const UiNode& node) {
            // every node of the subtree was numbered and walked (§6.1)
            m_visited++;
            // a node the layout gave area is placed, while a zero extent node occupies no cell of the frame (§8.2)
            if (!node.bounds().empty())
                m_laid_out++;
            // an invisible tree draws nothing, so no node is counted as drawn (§6.1)
            if (!draws || !node.visible())
                return;
            // a culled frame draws only the nodes whose subtree reaches the damage, where an empty one is whole (§6.1)
            if (damage && node.visual_bounds().intersect(*damage).empty())
                return;
            // the node reached the renderer, which is what the drawn count reports (§6.1)
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
