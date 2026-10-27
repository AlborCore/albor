#pragma once

#include <memory>

#include "core/geometry.h"
#include "render/shaped_run.h"

namespace albor
{
    // one measured run of text, which carries the shaped run the render stage replays (§9.1, AD-18)
    struct TextLayout
    {
        // the shaped run the measurement produced, which is null when nothing could be shaped (§9.1)
        std::shared_ptr<const ShapedRun> run{};

        // the size of the run in logical pixels
        Size size;

        // the ascent of the run, which is what a caller places content above (§9.1)
        float ascent = 0.0f;

        // the descent of the run, which is positive downwards (§9.1)
        float descent = 0.0f;

        // report whether the run carries no glyphs
        [[nodiscard]] bool empty() const;

        // compare two layouts by what they report, because a layout is a measurement and not an identity (§9.1)
        [[nodiscard]] friend bool operator==(const TextLayout& lhs, const TextLayout& rhs) {
            // the run counts only through whether it draws, so two runs that report the same extent are equal (§9.1)
            return lhs.empty() == rhs.empty() && lhs.size == rhs.size && lhs.ascent == rhs.ascent && lhs.descent == rhs.descent;
        }
    };
} // namespace albor
