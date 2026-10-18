#include "render/render-albor/albor_glyph_path.h"

namespace albor::render
{
    GlyphPath platform_glyph_path(const FontHandle& font, std::uint32_t glyph_id, float font_size) {
        // a platform this backend cannot walk a typeface on carries no outline yet, which #107 closes (§9.1)
        (void)font;
        (void)glyph_id;
        (void)font_size;
        return {};
    }
} // namespace albor::render
