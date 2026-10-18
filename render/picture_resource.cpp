#include "render/picture_resource.h"

#include <algorithm>
#include <utility>

namespace albor
{
    PictureResource::PictureResource(std::uint64_t id, std::vector<DrawCommand> commands) :
        m_id(id), m_commands(std::move(commands)) {}

    std::uint64_t PictureResource::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    const std::vector<DrawCommand>& PictureResource::commands() const {
        // the commands are handed back in recording order, which is the replay order (§6.4)
        return m_commands;
    }

    std::size_t PictureResource::command_count() const {
        // the count is the number of commands the replay walks (§6.4)
        return m_commands.size();
    }

    bool PictureResource::empty() const {
        // an empty picture is one that replays nothing (§6.4)
        return m_commands.empty();
    }

    PictureRecorder::PictureRecorder() :
        m_clip{.rect = DrawCommand::unclipped()} {}

    void PictureRecorder::save() {
        // the current state is pushed before it can be replaced, so a restore has something to return to (§6.2)
        m_states.push_back(State{.transform = m_transform, .clip = m_clip, .opacity = m_opacity});
    }

    void PictureRecorder::restore() {
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

    void PictureRecorder::set_transform(const Transform& transform) {
        // every later command stamps the transform the recorder stands at (§6.4)
        m_transform = transform;
    }

    const Transform& PictureRecorder::transform() const {
        // the transform is read back by content that composes on top of it (§6.2)
        return m_transform;
    }

    void PictureRecorder::clip_rect(const Rect& rect) {
        // the clip narrows rather than replaces, because a nested clip may only ever cut more away (§6.2)
        clip_to_rect(m_clip, rect);
    }

    void PictureRecorder::clip_rounded_rect(const Rect& rect, float radius) {
        // a rounded clip keeps its radius, which the replay scissors by (§6.2)
        clip_to_rounded_rect(m_clip, rect, radius);
    }

    const Rect& PictureRecorder::clip() const {
        // the clip is read back by content that tests visibility against it (§6.2)
        return m_clip.rect;
    }

    void PictureRecorder::set_opacity(float opacity) {
        // the opacity is clamped into the unit range, because content computes it from data (§6.1)
        m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    }

    float PictureRecorder::opacity() const {
        // the opacity is read back by content that composes its own on top of it (§6.1)
        return m_opacity;
    }

    void PictureRecorder::draw_rect(const Rect& rect, const Paint& paint) {
        // the rectangle stays raw, because the transform travels beside it (§6.4)
        record(DrawCommand::rectangle(rect, paint));
    }

    void PictureRecorder::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // the radii travel with the geometry, so a recorded command keeps them (§6.2)
        record(DrawCommand::rounded_rectangle(rect, radius, paint));
    }

    void PictureRecorder::draw_polyline(const std::span<const Point>& points, const StrokeStyle& stroke) {
        // every vertex is mapped through the recorder transform now, because the replay cannot ask where (§6.4)
        std::vector<Point> mapped;
        mapped.reserve(points.size());
        // the run keeps the shape the transform cooked, which is what stays readable in one pass (§6.4)
        for (const Point& point : points)
            mapped.push_back(m_transform.apply(point));
        // the mapped polyline records as the vertex run the replay joins (§6.2)
        record(DrawCommand::polyline(mapped, stroke));
    }

    void PictureRecorder::draw_shadow(const Rect& rect, float radius, const Shadow& shadow) {
        // a cast with no colour or no shape puts no ink down, so it is never recorded (§6.2)
        if (!shadow.casts() || rect.empty())
            return;
        // the cast travels raw, because the transform and the clip travel beside it (§6.4)
        record(DrawCommand::shadow(rect, radius, shadow));
    }

    void PictureRecorder::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // the origin stays raw, because the transform travels beside it (§6.4)
        record(DrawCommand::text(layout, origin, paint));
    }

    void PictureRecorder::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // the passed transform composes after the recorder's own, like every retained draw (§6.4)
        record(DrawCommand::line_batch(batch, transform.then(m_transform), stroke));
    }

    void PictureRecorder::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // the passed transform composes after the recorder's own, like every retained draw (§6.4)
        record(DrawCommand::path(path, transform.then(m_transform), paint));
    }

    void PictureRecorder::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture with no command draws nothing, so it is not recorded (§6.4, AD-06)
        if (!picture || picture->empty())
            return;
        // the passed transform sits below the recorder's own, which every replayed command inherits (§6.4)
        const Transform outer = transform.then(m_transform);
        // nesting is flattened, so the resulting picture stays a single ordered command list (§6.4)
        for (const DrawCommand& command : picture->commands()) {
            // the recorded transform stamps the command into the surrounding space (§6.4)
            DrawCommand stored = command;
            // the transforms compose, so a nested command lands where both parents put it (§6.4)
            stored.set_transform(command.transform().then(outer));
            // the outer clip wins, because it is the one the surrounding content asked for (§6.2)
            stored.set_clip(m_clip.rect);
            // the radius rides with the clip, which a rounded clip scissors by (§6.2)
            stored.set_clip_radius(m_clip.radius);
            // the stored command joins the list in recording order (§6.4)
            record(std::move(stored));
        }
    }

    void PictureRecorder::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // the destination stays raw, because the transform travels beside it (§6.4)
        record(DrawCommand::image(image, destination, paint));
    }

    const std::vector<DrawCommand>& PictureRecorder::commands() const {
        // the commands are handed back in recording order, which becomes the picture's replay order (§6.4)
        return m_commands;
    }

    bool PictureRecorder::empty() const {
        // an empty recorder is one that built nothing (§6.4)
        return m_commands.empty();
    }

    void PictureRecorder::reset() {
        // a reuse starts from a clean state and an empty command list (§6.4)
        m_states.clear();
        m_commands.clear();
        m_transform = Transform{};
        // the clip returns to the whole area, because a picture that was clipped records nothing under the old one (§6.2)
        m_clip = ClipState{.rect = DrawCommand::unclipped()};
    }

    void PictureRecorder::record(DrawCommand command) {
        // every command carries the clip the recorder stood at, because the replay scissors to it (§6.2)
        command.set_clip(m_clip.rect);
        // the radius rides with the clip, which a rounded clip scissors by (§6.2)
        command.set_clip_radius(m_clip.radius);
        // the cull is decided on the authored paint, because a draw the author made is a draw even at no opacity (§6.1)
        if (command.draws_nothing())
            return;
        // a fully opaque context leaves every paint alone, which is the common case (§6.1)
        if (m_opacity < 1.0f)
            command.scale_alpha(m_opacity);
        // a shape command draws under the transform the recorder stands at (§6.4)
        if (command.kind() == DrawCommandKind::Rect || command.kind() == DrawCommandKind::RoundedRect || command.kind() == DrawCommandKind::Text || command.kind() == DrawCommandKind::Image)
            command.set_transform(m_transform);
        // every accepted command is kept, because the picture replays in order (§6.4)
        m_commands.push_back(std::move(command));
    }
} // namespace albor
