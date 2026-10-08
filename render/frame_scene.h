#pragma once

#include <cstddef>
#include <vector>

#include "core/geometry.h"
#include "core/paint.h"
#include "render/draw_command.h"
#include "window/window_metrics.h"

namespace albor
{
    // the immutable scene a UI thread builds and a render thread consumes, which is one ordered command list (§6.1, AD-18)
    class FrameScene
    {
    public:
        // build an empty scene for the metrics the surface presents and the colour it is cleared with (§6.1)
        FrameScene(WindowMetrics metrics, Color background);

        // build a scene over rectangles, which is the shape an example that draws by hand hands over (§6.1)
        [[nodiscard]] static FrameScene from_primitives(WindowMetrics metrics, Color background, std::vector<Rect> rects);

        // hand out the metrics the scene was built for (§6.1)
        [[nodiscard]] const WindowMetrics& metrics() const;

        // hand out the background colour the surface is cleared with (§6.1)
        [[nodiscard]] Color background() const;

        // hand out the commands in paint order, which is the order the ui thread recorded them (§6.1)
        [[nodiscard]] const std::vector<DrawCommand>& commands() const;

        // hand out how many commands the scene replays (§6.1)
        [[nodiscard]] std::size_t command_count() const;

        // report whether the scene draws nothing but its background (§6.1)
        [[nodiscard]] bool empty() const;

        // add a command that a ui node recorded during scene build (§6.1)
        void add_command(DrawCommand command);

        // compare two scenes field by field, which a test asserts on (§6.1)
        friend bool operator==(const FrameScene& lhs, const FrameScene& rhs) = default;

        // compare two scenes for inequality, which a test asserts on (§6.1)
        friend bool operator!=(const FrameScene& lhs, const FrameScene& rhs) = default;

    private:
        // the metrics the scene is built for, which the render thread presents (§6.1)
        WindowMetrics m_metrics;

        // the background the surface is cleared with before any command draws (§6.1)
        Color m_background;

        // the commands in paint order, which is what the render thread replays (§6.1, AD-18)
        std::vector<DrawCommand> m_commands;
    };
} // namespace albor
