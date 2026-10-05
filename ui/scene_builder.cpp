#include "ui/scene_builder.h"

namespace albor
{
    SceneRecorder::SceneRecorder(WindowMetrics metrics) :
        m_metrics(metrics) {}

    void SceneRecorder::save() {
        // the current state is pushed before it can be replaced, so a restore has something to return to (§6.2)
        m_states.push_back(State{.transform = m_transform, .clip = m_clip});
    }

    void SceneRecorder::restore() {
        // a restore with nothing pushed would pop below the empty state stack (§6.2)
        if (m_states.empty())
            return;
        const State& last_state = m_states.back();
        // the state returns to what the matching save pushed (§6.2)
        m_transform = last_state.transform;
        m_clip = last_state.clip;
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
        // a scene clip is recorded rather than intersected, because stage two replays the geometry as is (§6.1)
        m_clip = rect;
    }

    void SceneRecorder::clip_rounded_rect(const Rect& rect, float) {
        // a rounded clip reduces to its rectangle in a recorded scene, which is what stage two draws (§6.1)
        m_clip = rect;
    }

    const Rect& SceneRecorder::clip() const {
        // the clip is read back by a node that tests visibility against it (§6.2)
        return m_clip;
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

    void SceneRecorder::draw_rect(const Rect& rect, const Paint& paint) {
        // a rectangle with no area contributes nothing to the frame (§6.2)
        if (rect.empty())
            return;
        // the colour travels with the geometry, so stage two draws what stage one recorded (§6.1, AD-18)
        m_rect_colors.push_back(paint.fill);
        m_rects.push_back(to_scene(rect));
    }

    void SceneRecorder::draw_rounded_rect(const Rect& rect, float, const Paint& paint) {
        // a rounded rectangle records as its rectangle, which is the level one primitive the scene carries (§6.1)
        draw_rect(rect, paint);
    }

    void SceneRecorder::draw_text(const TextLayout& layout, Point origin, const Paint&) {
        // a run with no glyphs costs nothing, so it is not recorded (§6.2)
        if (layout.empty())
            return;
        // the origin is mapped now, because the render thread cannot ask where the ui thread drew it (§6.1, AD-18)
        m_text_origins.push_back(to_scene(origin));
        m_texts.push_back(layout);
    }

    void SceneRecorder::set_background(Color background) {
        // the background is the clear colour of the whole frame (§6.1)
        m_background = background;
    }

    std::shared_ptr<const FrameScene> SceneRecorder::scene() const {
        // the recorded rectangles build the phase one scene shape, which is what stage two replays (§6.1)
        std::shared_ptr<FrameScene> scene = std::make_shared<FrameScene>(FrameScene::from_primitives(m_metrics, m_background, m_rects));
        // the colours travel with the rectangles, because the render thread owns the canvas and cannot ask (§6.1, AD-18)
        scene->set_rect_colors(m_rect_colors);
        // the text runs are added afterwards, because from_primitives carries the rectangles only (§6.1)
        for (std::size_t index = 0; index < m_texts.size(); index++)
            scene->add_text(m_texts[index], m_text_origins[index]);
        // the scene is const from here, because the render thread only ever reads it (§6.1, AD-18)
        return std::shared_ptr<const FrameScene>(std::move(scene));
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
        // the root is measured against the window, which is the whole of the available space (§8.2)
        Size measured{};
        root.measure(SizeConstraints::tight(metrics.logical_size), measured);
        m_laid_out++;
        // the root is arranged to the full window, because a ui tree covers its surface (§8.2)
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        SceneRecorder recorder(metrics);
        recorder.set_background(background);
        recorder.save();
        // an invisible tree draws nothing, so the scene carries only the background (§6.1)
        if (root.visible())
            root.render(recorder);
        recorder.restore();
        m_drawn = recorder.depth();
        // the whole subtree counts as visited, because the layout and the walk touched all of it (§6.1)
        root.visit([this](const UiNode&) { m_visited++; });
        // the dirty flags clear only after a successful build, so a failed build keeps them (§6.1)
        root.clear_dirty();
        return recorder.scene();
    }

    std::size_t SceneBuilder::visited_count() const { return m_visited; }

    std::size_t SceneBuilder::drawn_count() const { return m_drawn; }

    std::size_t SceneBuilder::laid_out_count() const { return m_laid_out; }

    NodeId SceneBuilder::last_node_id() const { return m_last_node; }
} // namespace albor
