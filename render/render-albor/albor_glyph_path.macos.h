#pragma once

#include <CoreGraphics/CoreGraphics.h>

#include "render/render-albor/albor_glyph_path.h"

namespace albor::render
{
    // walk one core graphics path into the verbs and points a contour is built from (§6.4, §9.1)
    [[nodiscard]] GlyphPath glyph_path_from_cgpath(CGPathRef path);
} // namespace albor::render
