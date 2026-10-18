#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry.h"
#include "render/font_handle.h"
#include "render/path_resource.h"

namespace albor::render
{
    // one glyph's outline in logical pixels, whose baseline sits at the origin and whose y axis points down (§9.1, AD-27)
    struct GlyphPath
    {
        // the verbs of every subpath, which the contour builder walks in order (§6.4)
        std::vector<PathVerb> verbs;

        // the points those verbs consume, in the order they are recorded (§6.4)
        std::vector<Point> points;
    };

    // walk the outline a platform typeface carries for one glyph, or an empty path when it carries none (§9.1, AD-28)
    [[nodiscard]] GlyphPath platform_glyph_path(const FontHandle& font, std::uint32_t glyph_id, float font_size);
} // namespace albor::render
