#include "render/frame_scene.h"

#include <utility>

namespace albor
{
    FrameScene::FrameScene(WindowMetrics metrics, Color background) :
        m_metrics(metrics), m_background(background) {}

    FrameScene FrameScene::from_primitives(WindowMetrics metrics, Color background, std::vector<Rect> rects) {
        // the scene is filled once and never touched again, because the render thread consumes it (§6.1)
        FrameScene scene(metrics, background);
        // the whole surface is the clip every hand drawn rectangle sits under, because nothing narrows it (§6.1)
        const Rect full = Rect::from_origin_size(Point{0.0f, 0.0f}, metrics.logical_size);
        // the rectangles keep the order they were given in, which is their paint order (§6.1)
        for (const Rect& rect : rects) {
            // the rectangle is recorded as a command, so the scene keeps one shape for every draw (§6.1)
            DrawCommand command = DrawCommand::rectangle(rect, Paint{.fill = Color::rgb(0.20f, 0.55f, 0.90f)});
            // the clip is stamped, because a command carries its own (§6.2)
            command.set_clip(full);
            // the command joins the list in the order the caller drew (§6.1)
            scene.add_command(std::move(command));
        }
        return scene;
    }

    const WindowMetrics& FrameScene::metrics() const {
        // the metrics travel with the scene, so the render thread knows what it was built for (§6.1)
        return m_metrics;
    }

    Color FrameScene::background() const {
        // the background covers the whole logical size of the surface (§6.1)
        return m_background;
    }

    const std::vector<DrawCommand>& FrameScene::commands() const {
        // the commands replay in the order the ui thread recorded them, which is the paint order (§6.1)
        return m_commands;
    }

    std::size_t FrameScene::command_count() const {
        // the count is what a test asserts on, and what a test double walks (§6.1)
        return m_commands.size();
    }

    bool FrameScene::empty() const {
        // a scene with no command draws its background and nothing else (§6.1)
        return m_commands.empty();
    }

    void FrameScene::add_command(DrawCommand command) {
        // a command that would draw nothing is refused at the boundary, because the surface would pay for it (§6.2)
        if (command.draws_nothing())
            return;
        // the command joins the list in paint order, which is the order the ui thread recorded (§6.1)
        m_commands.push_back(std::move(command));
    }
} // namespace albor
