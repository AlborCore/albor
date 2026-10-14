#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "core/paint.h"

namespace albor
{
    class FrameScene;
    struct WindowMetrics;

    // what builds the immutable scene of one tick when an application draws by hand rather than through a tree (§6.1)
    using SceneSource = std::function<std::shared_ptr<const FrameScene>(const WindowMetrics& metrics, std::uint64_t frame_index)>;

    // how a main window builds and presents its frames, which the application applies once over that window's stage (§6.1)
    struct MainWindowConfig
    {
        // the colour the scene is cleared with every frame, which the widgets read against (§6.1)
        Color background = Color::rgb(0.05f, 0.06f, 0.08f);

        // whether a tick that found nothing dirty skips the frame, which is what a retained tree wants (§6.1)
        bool build_only_when_dirty = false;

        // the source that replaces the pipeline's own build, which an application that owns its scene installs (§6.1)
        SceneSource scene_source;
    };
} // namespace albor
