#pragma once

#include <cstdint>

#include "render/font_handle.h"
#include "render/glyph_outline.h"

namespace albor::render
{
    // one glyph's outline in logical pixels, which the face reports and the cache stores as one stream (§6.4, §9.1)
    using GlyphPath = GlyphOutlineData;

    // walk the outline a run's font carries for one glyph, or an empty path when it carries none (§9.1, AD-34)
    [[nodiscard]] GlyphPath glyph_path(const FontHandle& font, std::uint32_t glyph_id, float font_size);
} // namespace albor::render
