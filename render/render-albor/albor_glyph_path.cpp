#include "render/render-albor/albor_glyph_path.h"

#include <memory>

#include "render/font_face.h"

namespace albor::render
{
    GlyphPath glyph_path(const FontHandle& font, std::uint32_t glyph_id, float font_size) {
        // a handle with no font and a size of no pixels carry no outline, which the walk refuses (§9.1)
        if (!font.valid() || font_size <= 0.0f)
            return {};
        // the font a run shaped from keeps its own bytes, so the walk needs no platform typeface (§9.1, AD-34)
        const std::shared_ptr<const FontFace>& face = font.face();
        // the face reports the same stream the cache stores, already in logical pixels and y down (§6.4, §9.1)
        return face->outline(glyph_id, font_size);
    }
} // namespace albor::render
