#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "render/frame_scene.h"

namespace albor::inspector
{
    // one thing the inspector found wrong with a frame, which a developer reads while running (§3.1, §12.2)
    struct Finding
    {
        // the rule that fired, which is what a developer searches the source for (§12.2)
        std::string code;

        // what was found, stated so it can be acted on without reading the rule (§12.2)
        std::string message;
    };

    // what one frame holds, which is what a scene debugger prints (§3.1, §12.2)
    struct SceneReport
    {
        // how many commands the frame recorded (§6.1)
        std::size_t commands = 0;

        // how many of each kind, in the enumeration's order, which is what shows at a glance (§12.2)
        std::vector<std::size_t> per_kind;

        // the area every command covered, which is what a bounds overlay draws (§6.2)
        Rect covered;

        // what the inspector found wrong, which is empty when nothing is (§12.2)
        std::vector<Finding> findings;
    };

    // describe one frame, which is the whole of a scene debugger (§3.1, §12.2)
    [[nodiscard]] SceneReport inspect(const FrameScene& scene);

    // render the frames' bounds as an overlay scene, which is the bounds visualizer (§3.1, §12.2)
    [[nodiscard]] std::shared_ptr<const FrameScene> bounds_overlay(const FrameScene& scene, Color tint);
} // namespace albor::inspector
