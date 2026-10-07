#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/frame_scene.h"
#include "render/image_resource.h"
#include "render/line_batch.h"
#include "render/path_resource.h"
#include "render/render_context.h"

namespace albor
{
    // how one picture command draws, which the replay switches on (§6.4, AD-06)
    enum class PictureCommandKind
    {
        // a filled and stroked rectangle
        Rect,

        // a filled and stroked rectangle with corner radii
        RoundedRect,

        // a text run at its baseline origin
        Text,

        // a retained immutable line batch under a transform
        LineBatch,

        // a retained immutable path under a transform
        Path,

        // an immutable image into a destination rectangle
        Image,
    };

    // one recorded command of a picture, held in raw local coordinates with the transform the recorder stood at (§6.4, AD-06)
    struct PictureCommand
    {
        // which payload the command carries (§6.4)
        PictureCommandKind kind = PictureCommandKind::Rect;

        // the rectangle, rounded rectangle or image destination (§6.4)
        Rect rect{};

        // the corner radii of a rounded rectangle (§6.4)
        float radius = 0.0f;

        // the text run and its baseline origin (§6.4)
        TextLayout text{};

        // the baseline origin of the run in local coordinates (§6.4)
        Point origin{};

        // the retained batch or path the command replays (§6.4)
        std::shared_ptr<const LineBatch> batch{};

        // the retained path the command replays (§6.4)
        std::shared_ptr<const PathResource> path{};

        // the immutable image the command draws (§6.4)
        std::shared_ptr<const ImageResource> image{};

        // the transform the recorder stood at when the command was recorded (§6.4)
        Transform transform{};

        // the fill and stroke of the shape, run or image (§6.4)
        Paint paint{};

        // the stroke description of a line batch (§6.4)
        StrokeStyle stroke{};
    };

    // the immutable recorded display list a domain view retains off the ui thread, which replays in order (§6.4, AD-06)
    class PictureResource
    {
    public:
        // build a picture of the given commands under a factory identifier (§6.4)
        PictureResource(std::uint64_t id, std::vector<PictureCommand> commands);

        PictureResource(const PictureResource&) = delete;
        PictureResource& operator=(const PictureResource&) = delete;
        PictureResource(PictureResource&&) = delete;
        PictureResource& operator=(PictureResource&&) = delete;
        ~PictureResource() = default;

        // hand out the identifier the factory stamped the picture with, which the backend keys its gpu cache on (§6.4, AD-06)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the commands in recording order, which is the replay order (§6.4)
        [[nodiscard]] const std::vector<PictureCommand>& commands() const;

        // hand out how many commands the picture replays (§6.4)
        [[nodiscard]] std::size_t command_count() const;

        // report whether the picture replays nothing (§6.4)
        [[nodiscard]] bool empty() const;

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the commands in recording order (§6.4)
        std::vector<PictureCommand> m_commands;
    };

    // the render context that records into a picture rather than a scene, which a worker thread drives to build retained content (§6.4, AD-06)
    class PictureRecorder final : public RenderContext
    {
    public:
        // build a recorder over no commands (§6.4)
        PictureRecorder();

        PictureRecorder(const PictureRecorder&) = delete;
        PictureRecorder& operator=(const PictureRecorder&) = delete;
        PictureRecorder(PictureRecorder&&) = delete;
        PictureRecorder& operator=(PictureRecorder&&) = delete;
        ~PictureRecorder() = default;

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

        // override: record a filled and stroked rectangle
        void draw_rect(const Rect& rect, const Paint& paint) override;

        // override: record a filled and stroked rounded rectangle
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;

        // override: record a text run at its baseline origin
        void draw_text(const TextLayout& layout, Point origin, const Paint& paint) override;

        // override: record a retained line batch under a transform
        void draw_line_batch(const std::shared_ptr<const LineBatch>& batch, const Transform& transform, const StrokeStyle& stroke) override;

        // override: record a retained path under a transform
        void draw_path(const std::shared_ptr<const PathResource>& path, const Transform& transform, const Paint& paint) override;

        // override: replay a picture into this one, so the commands stay flat and in order
        void draw_picture(const std::shared_ptr<const PictureResource>& picture, const Transform& transform) override;

        // override: record an image into a destination rectangle
        void draw_image(const std::shared_ptr<const ImageResource>& image, const Rect& destination, const Paint& paint) override;

        // hand out the recorded commands in order, which the factory turns into a picture (§6.4)
        [[nodiscard]] const std::vector<PictureCommand>& commands() const;

        // report whether no command was recorded (§6.4)
        [[nodiscard]] bool empty() const;

        // drop every recorded command and reset the state, which a reuse starts from (§6.4)
        void reset();

    private:
        // the transform and clip one save pushed (§6.2)
        struct State
        {
            // the transform at the time of the save
            Transform transform;

            // the clip at the time of the save
            Rect clip;
        };

        // record a command after skipping the ones that would draw nothing (§6.4)
        void record(PictureCommand command);

        // the current transform the commands capture (§6.2)
        Transform m_transform;

        // the current clip the recorder reads back (§6.2)
        Rect m_clip;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> m_states;

        // the recorded commands in recording order (§6.4)
        std::vector<PictureCommand> m_commands;
    };
} // namespace albor
