#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/draw_command.h"
#include "render/render_context.h"

namespace albor
{
    // the immutable recorded display list a domain view retains off the ui thread, which replays in order (§6.4, AD-06)
    class PictureResource
    {
    public:
        // build a picture of the given commands under a factory identifier (§6.4)
        PictureResource(std::uint64_t id, std::vector<DrawCommand> commands);

        PictureResource(const PictureResource&) = delete;
        PictureResource& operator=(const PictureResource&) = delete;
        PictureResource(PictureResource&&) = delete;
        PictureResource& operator=(PictureResource&&) = delete;
        ~PictureResource() = default;

        // hand out the identifier the factory stamped the picture with, which the backend keys its gpu cache on (§6.4, AD-06)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the commands in recording order, which is the replay order (§6.4)
        [[nodiscard]] const std::vector<DrawCommand>& commands() const;

        // hand out how many commands the picture replays (§6.4)
        [[nodiscard]] std::size_t command_count() const;

        // report whether the picture replays nothing (§6.4)
        [[nodiscard]] bool empty() const;

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the commands in recording order (§6.4)
        std::vector<DrawCommand> m_commands;
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

        // override: set the opacity the later draws are dimmed by
        void set_opacity(float opacity) override;

        // override: read back the current opacity
        [[nodiscard]] float opacity() const override;

        // override: record a filled and stroked rectangle
        void draw_rect(const Rect& rect, const Paint& paint) override;

        // override: record a filled and stroked rounded rectangle
        void draw_rounded_rect(const Rect& rect, float radius, const Paint& paint) override;

        // override: record the shadow a shape casts
        void draw_shadow(const Rect& rect, float radius, const Shadow& shadow) override;

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
        [[nodiscard]] const std::vector<DrawCommand>& commands() const;

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

            // the opacity at the time of the save
            float opacity = 1.0f;
        };

        // record a command after skipping the ones that would draw nothing (§6.4)
        void record(DrawCommand command);

        // the current transform the commands capture (§6.2)
        Transform m_transform;

        // the current clip the recorder reads back, which starts as the whole area because a picture has no surface (§6.2)
        Rect m_clip;

        // the opacity the recorded commands are dimmed by, which starts fully opaque (§6.1)
        float m_opacity = 1.0f;

        // the pushed states, which a restore pops (§6.2)
        std::vector<State> m_states;

        // the recorded commands in recording order (§6.4)
        std::vector<DrawCommand> m_commands;
    };
} // namespace albor
