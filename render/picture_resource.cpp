#include "render/picture_resource.h"

namespace albor
{
    PictureResource::PictureResource(std::uint64_t id, std::vector<PictureCommand> commands) :
        m_id(id), m_commands(std::move(commands)) {}

    std::uint64_t PictureResource::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    const std::vector<PictureCommand>& PictureResource::commands() const {
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

    PictureRecorder::PictureRecorder() = default;

    void PictureRecorder::save() {
        // the current state is pushed before it can be replaced, so a restore has something to return to (§6.2)
        m_states.push_back(State{.transform = m_transform, .clip = m_clip});
    }

    void PictureRecorder::restore() {
        // a restore with nothing pushed would pop below the empty state stack (§6.2)
        if (m_states.empty())
            return;
        const State& last_state = m_states.back();
        // the state returns to what the matching save pushed (§6.2)
        m_transform = last_state.transform;
        m_clip = last_state.clip;
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
        // the clip is stored rather than intersected, because the picture replays the geometry as is (§6.2)
        m_clip = rect;
    }

    void PictureRecorder::clip_rounded_rect(const Rect& rect, float) {
        // a rounded clip reduces to its rectangle in a picture, which is what the replay clips to (§6.2)
        m_clip = rect;
    }

    const Rect& PictureRecorder::clip() const {
        // the clip is read back by content that tests visibility against it (§6.2)
        return m_clip;
    }

    void PictureRecorder::draw_rect(const Rect& rect, const Paint& paint) {
        // a rectangle with no area contributes nothing to the frame (§6.2)
        if (rect.empty())
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::Rect;
        // the rectangle stays raw, because the transform travels beside it (§6.4)
        command.rect = rect;
        command.transform = m_transform;
        command.paint = paint;
        record(std::move(command));
    }

    void PictureRecorder::draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) {
        // a rectangle with no area contributes nothing to the frame (§6.2)
        if (rect.empty())
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::RoundedRect;
        // the rectangle stays raw, because the transform travels beside it (§6.4)
        command.rect = rect;
        command.radius = radius;
        command.transform = m_transform;
        command.paint = paint;
        record(std::move(command));
    }

    void PictureRecorder::draw_text(const TextLayout& layout, Point origin, const Paint& paint) {
        // a run with no glyphs costs nothing, so it is not recorded (§6.2)
        if (layout.empty())
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::Text;
        command.text = layout;
        // the origin stays raw, because the transform travels beside it (§6.4)
        command.origin = origin;
        command.transform = m_transform;
        command.paint = paint;
        record(std::move(command));
    }

    void PictureRecorder::draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) {
        // a batch with no segment draws nothing, so it is not recorded (§6.4, AD-06)
        if (!batch || batch->segment_count() == 0)
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::LineBatch;
        command.batch = batch;
        // the passed transform composes after the recorder's own, like every retained draw (§6.4)
        command.transform = transform.then(m_transform);
        command.stroke = stroke;
        record(std::move(command));
    }

    void PictureRecorder::draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) {
        // a path with no command draws nothing, so it is not recorded (§6.4, AD-06)
        if (!path || path->verbs().empty())
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::Path;
        command.path = path;
        // the passed transform composes after the recorder's own, like every retained draw (§6.4)
        command.transform = transform.then(m_transform);
        command.paint = paint;
        record(std::move(command));
    }

    void PictureRecorder::draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) {
        // a picture with no command draws nothing, so it is not recorded (§6.4, AD-06)
        if (!picture || picture->empty())
            return;
        // the passed transform sits below the recorder's own, which every replayed command inherits (§6.4)
        const Transform outer = transform.then(m_transform);
        // nesting is flattened, so the resulting picture stays a single ordered command list (§6.4)
        for (const PictureCommand& command : picture->commands()) {
            // the recorded transform stamps the command into the surrounding space (§6.4)
            PictureCommand stored = command;
            stored.transform = command.transform.then(outer);
            record(std::move(stored));
        }
    }

    void PictureRecorder::draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) {
        // an image with no pixel or no destination draws nothing, so it is not recorded (§6.4, AD-06)
        if (!image || image->pixels().empty() || destination.empty())
            return;
        PictureCommand command;
        command.kind = PictureCommandKind::Image;
        // the destination stays raw, because the transform travels beside it (§6.4)
        command.rect = destination;
        command.image = image;
        command.transform = m_transform;
        command.paint = paint;
        record(std::move(command));
    }

    const std::vector<PictureCommand>& PictureRecorder::commands() const {
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
        m_clip = Rect{};
    }

    void PictureRecorder::record(PictureCommand command) {
        // every accepted command is kept, because the picture replays in order (§6.4)
        m_commands.push_back(std::move(command));
    }
} // namespace albor
