#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "render/font_face.h"

namespace albor::harfbuzz
{
    // the font a shaped run draws from, which owns its bytes and outlines a glyph with no platform typeface (§9.1)
    class HarfbuzzFontFace final : public FontFace
    {
    public:
        // build over the bytes a run was shaped from, or null when the bytes carry no font (§9.1, AD-34)
        [[nodiscard]] static std::shared_ptr<const HarfbuzzFontFace> create(std::shared_ptr<const std::vector<std::uint8_t>> bytes);

        ~HarfbuzzFontFace() override;

        HarfbuzzFontFace(const HarfbuzzFontFace&) = delete;
        HarfbuzzFontFace& operator=(const HarfbuzzFontFace&) = delete;
        HarfbuzzFontFace(HarfbuzzFontFace&&) = delete;
        HarfbuzzFontFace& operator=(HarfbuzzFontFace&&) = delete;

        // the content hash of the bytes, which is the identity a layout and a cache key on (§15, AD-34)
        [[nodiscard]] std::uint64_t hash() const override;

        // the sfnt bytes the face was built from, which a backend or a tool may read (§15, AD-34)
        [[nodiscard]] std::span<const std::uint8_t> bytes() const override;

        // one glyph's outline at a size, which the render thread walks with no engine state (§6.4, §9.1, AD-18)
        [[nodiscard]] GlyphOutlineData outline(std::uint32_t glyph_id, float font_size) const override;

    private:
        // the harfbuzz handles and the bytes they read, so no shaping type leaves this header (§3.5, AD-19)
        struct State;

        // build over the state the caller prepared, which is how the factory keeps the handles private (§9.1)
        explicit HarfbuzzFontFace(std::unique_ptr<State> state);

        // the blob, the face and the bytes they borrow (§9.1, AD-34)
        std::unique_ptr<State> m_state;
    };
} // namespace albor::harfbuzz
