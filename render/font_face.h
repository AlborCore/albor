#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "render/glyph_outline.h"

namespace albor
{
    // the font a shaped run draws from, which keeps its own bytes and outlines a glyph on any backend (§9.1, AD-34)
    class FontFace
    {
    public:
        FontFace() = default;
        virtual ~FontFace() = default;

        FontFace(const FontFace&) = delete;
        FontFace& operator=(const FontFace&) = delete;
        FontFace(FontFace&&) = delete;
        FontFace& operator=(FontFace&&) = delete;

        // the content hash of the bytes, which is the identity a layout and a cache key on (§15, AD-34)
        [[nodiscard]] virtual std::uint64_t hash() const = 0;

        // the sfnt bytes the face was built from, which a backend or a tool may read (§15, AD-34)
        [[nodiscard]] virtual std::span<const std::uint8_t> bytes() const = 0;

        // one glyph's outline in logical pixels, baseline at the origin and y down, or empty for a glyph with none (§6.4)
        [[nodiscard]] virtual GlyphOutlineData outline(std::uint32_t glyph_id, float font_size) const = 0;
    };
} // namespace albor
