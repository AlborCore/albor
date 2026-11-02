#pragma once

#include <cstdint>
#include <memory>

namespace albor
{
    // the font a shaped run draws from, which owns its bytes and outlines a glyph with no platform typeface (§9.1)
    class FontFace;

    // an opaque reference to the font a run drew from, so no shaping or renderer type reaches a public header (§3.5)
    class FontHandle
    {
    public:
        FontHandle() = default;

        // wrap the font a run shaped from, which carries the bytes any backend outlines a glyph through (§9.1, AD-34)
        [[nodiscard]] static FontHandle from_face(std::shared_ptr<const FontFace> face);

        // report whether the handle names a font at all, which a handle the engine never filled does not (§9.1)
        [[nodiscard]] bool valid() const;

        // the font a run shaped from, which a renderer outlines through (§9.1, AD-34)
        [[nodiscard]] const std::shared_ptr<const FontFace>& face() const;

        // the content hash of that font, which a layout keys on, or zero when the handle carries none (§15)
        [[nodiscard]] std::uint64_t font_hash() const;

        // compare two handles by the font object they hold, which is one per resolution (§9.1)
        friend bool operator==(const FontHandle& lhs, const FontHandle& rhs) = default;

    private:
        // the font the run shaped from, which the handle keeps alive across the engine that resolved it (§9.1, AD-34)
        std::shared_ptr<const FontFace> m_face;
    };
} // namespace albor
