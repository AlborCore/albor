#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "render/font_handle.h"

namespace albor
{
    // one positioned glyph, which is the unit a shaped run is made of (§9.1)
    struct ShapedGlyph
    {
        // the glyph index inside the typeface of its line, which is what a renderer rasterizes (§9.1)
        std::uint32_t id = 0;

        // where the glyph sits relative to the baseline origin of its line, in logical pixels (§9.1)
        Point position;

        // compare two glyphs component wise (§9.1)
        friend bool operator==(const ShapedGlyph& lhs, const ShapedGlyph& rhs) = default;
    };

    // the immutable shaped run a ui thread measures and a render thread replays verbatim (§9.1, §10.5, AD-18)
    class ShapedRun
    {
    public:
        // one laid out line of the run, which carries the typeface its glyphs came from (§9.1)
        struct Line
        {
            // the typeface this line was shaped with, which is per line because a fallback can split it (§9.1)
            FontHandle font;

            // the baseline origin of the line, relative to the run origin (§9.1)
            Point origin;

            // the glyphs of the line in drawing order (§9.1)
            std::vector<ShapedGlyph> glyphs;

            // compare two lines component wise (§9.1)
            friend bool operator==(const Line& lhs, const Line& rhs) = default;
        };

        // build a run over its lines and the size, with the content hash that identifies the font it came from (§15)
        [[nodiscard]] static std::shared_ptr<const ShapedRun> make(std::vector<Line> lines, float font_size, std::uint64_t font_hash = 0);

        // the content hash of the font this run was shaped from, which is zero when the engine has none (§15, AD-34)
        [[nodiscard]] std::uint64_t font_hash() const;

        // the font size the whole run was shaped at, which a renderer builds its font from (§9.1)
        [[nodiscard]] float font_size() const;

        // the shaped lines in reading order, where every line places its own baseline (§9.1)
        [[nodiscard]] const std::vector<Line>& lines() const;

        // report whether the run carries no glyph at all, which costs nothing to draw (§6.2)
        [[nodiscard]] bool empty() const;

        // how many glyphs the run carries across every line, which a test asserts on (§9.1)
        [[nodiscard]] std::size_t glyph_count() const;

        // compare two runs field by field (§9.1)
        friend bool operator==(const ShapedRun& lhs, const ShapedRun& rhs) = default;

    private:
        // a run is only ever built through make, because it is shared as an immutable snapshot (§10.5, AD-18)
        explicit ShapedRun(std::vector<Line> lines, float font_size, std::uint64_t font_hash);

        // the shaped lines of the run, which never change once the run exists (§10.5)
        std::vector<Line> m_lines;

        // the size every line was shaped at, which a renderer needs to build its font (§9.1)
        float m_font_size = 0.0f;

        // the content hash of the font this run was shaped from, which identifies it across platforms (§15, AD-34)
        std::uint64_t m_font_hash = 0;
    };
} // namespace albor
