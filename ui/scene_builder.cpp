#include "ui/scene_builder.h"

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
        // the clip is stamped rather than intersected, because a command carries the clip it draws under (§6.2)
        m_clip = rect;
    }

    void SceneRecorder::clip_rounded_rect(const Rect& rect, float) {
        // a rounded clip reduces to its rectangle, which is what the replay scissors to (§6.2)
        m_clip = rect;
    }

    const Rect& SceneRecorder::clip() const {
        // the clip is read back by a node that tests visibility against it (§6.2)
        return m_clip;
    }

    void SceneRecorder::draw_rect(const Rect& rect, const Paint& paint) {
        // the rectangle is mapped into scene coordinates now, because the render thread cannot ask where (§6.1, AD-18)
        record(DrawCommand::rectangle(to_scene(rect), paint));
    }

    void SceneRecorder::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the radii travel with the geometry, which is what the bucketed scene used to lose (§6.2)
        record(DrawCommand::rounded_rectangle(to_scene(rect), radius, paint));
    }

    void SceneRecorder::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // the origin is mapped now, because the render thread cannot ask where the ui thread drew it (§6.1, AD-18)
        record(DrawCommand::text(layout, to_scene(origin), paint));
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
            scene->add_command(command);
        return scene;
    }

    void SceneRecorder::record(DrawCommand command) {
        // the command is refused when it would draw nothing, so the scene carries no dead draw (§6.2)
        if (command.draws_nothing())
            return;
        // the scene holds the recorded commands in paint order, which is what stage two replays (§6.1, AD-18)
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
        // the root is measured against the window, which is the whole of the available space (§8.2)
        Size measured{};
        root.measure(SizeConstraints::tight(metrics.logical_size), measured);
        // the root is arranged to the full window, because a ui tree covers its surface (§8.2)
        root.arrange(Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size));
        SceneRecorder recorder(metrics);
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

    std::size_t SceneBuilder::visited_count() const { return m_visited; }

    std::size_t SceneBuilder::drawn_count() const { return m_drawn; }

    std::size_t SceneBuilder::laid_out_count() const { return m_laid_out; }

    NodeId SceneBuilder::last_node_id() const { return m_last_node; }
} // namespace albor
