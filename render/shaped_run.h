#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/geometry.h"
#include "render/font_handle.h"

namespace albor
{
    // how a shaped run reaches a surface, which decides the command a recorder emits for it (§9.1, AD-34)
    enum class ShapeSource : std::uint8_t
    {
        // the run carries a font its renderer resolves by content, so the recorder leaves it whole (§9.1, AD-34)
        Platform,

        // the run's glyphs reach any surface as tessellated outlines, which is the portable path of AD-34 (§15, AD-34)
        Embedded,
    };

    // one positioned glyph, which is the unit a shaped run is made of (§9.1)
    struct ShapedGlyph
    {
        // the glyph index inside the typeface of its line, which is what a renderer rasterizes (§9.1)
        std::uint32_t id = 0;

        // where the glyph sits relative to the baseline origin of its line, in logical pixels (§9.1)
        Point position;

        // the byte offset into the shaped text this glyph carries, which is what maps a glyph to a character (§9.1)
        std::uint32_t cluster = 0;

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

            // the total advance of the line in logical pixels, which is where a caret stands past its last glyph (§9.1)
            float width = 0.0f;

            // the ascent of this line, which is per line because a fallback or a tall run moves it (§9.1)
            float ascent = 0.0f;

            // the descent of this line, which is positive downwards and per line for the same reason (§9.1)
            float descent = 0.0f;

            // the byte offset the line begins at, which is what places an empty line in the run (§9.1)
            std::size_t start_byte = 0;

            // the byte offset the line ends at, which includes the break that ended it (§9.1)
            std::size_t end_byte = 0;

            // compare two lines component wise (§9.1)
            friend bool operator==(const Line& lhs, const Line& rhs) = default;
        };

        // build a run over its lines and the size, with the identity of the font it came from and its source (§15)
        [[nodiscard]] static std::shared_ptr<const ShapedRun> make(std::vector<Line> lines, float font_size, std::uint64_t font_hash = 0, ShapeSource source = ShapeSource::Embedded);

        // the content hash of the font this run was shaped from, which is zero when the engine has none (§15, AD-34)
        [[nodiscard]] std::uint64_t font_hash() const;

        // how this run reaches a surface, which a recorder reads to choose the command it emits (§9.1, AD-34)
        [[nodiscard]] ShapeSource shape_source() const;

        // the font size the whole run was shaped at, which a renderer builds its font from (§9.1)
        [[nodiscard]] float font_size() const;

        // the shaped lines in reading order, where every line places its own baseline (§9.1)
        [[nodiscard]] const std::vector<Line>& lines() const;

        // report whether the run carries no glyph at all, which costs nothing to draw (§6.2)
        [[nodiscard]] bool empty() const;

        // how many glyphs the run carries across every line, which a test asserts on (§9.1)
        [[nodiscard]] std::size_t glyph_count() const;

        // compare two runs by shape, which is ids and positions; a handle is not the face's identity (§9.1, AD-34)
        bool operator==(const ShapedRun& rhs) const {
            // the size and the hash gate first, because another font or size shapes another run (§9.1, AD-34)
            if (m_font_size != rhs.m_font_size || m_font_hash != rhs.m_font_hash)
                return false;
            // the lines are compared at their own shape, which is origins and glyphs (§9.1)
            return m_lines.size() == rhs.m_lines.size() && std::equal(m_lines.begin(), m_lines.end(), rhs.m_lines.begin(), [](const Line& lhs, const Line& rhs) {
                       // the origin is where the line hangs from, and the glyphs are the shape the run is (§9.1)
                       return lhs.origin == rhs.origin && lhs.glyphs == rhs.glyphs;
                   });
        }

        // the line a byte offset falls on, clamped to the last line, which resolves a caret to a row (§9.1)
        [[nodiscard]] std::size_t line_at_byte(std::size_t byte) const;

        // the horizontal offset within a line where a byte offset sits, which is where a caret is drawn (§9.1)
        [[nodiscard]] float x_at_byte(std::size_t line_index, std::size_t byte) const;

        // the byte offset in a line nearest a horizontal position, which is what a press resolves to (§9.1)
        [[nodiscard]] std::size_t byte_at_x(std::size_t line_index, float x) const;

        // the byte offset after the one given, which steps a caret to the next character of the line (§9.1)
        [[nodiscard]] std::size_t next_boundary_byte(std::size_t line_index, std::size_t byte) const;

        // the byte offset before the one given, which steps a caret back over one character of the line (§9.1)
        [[nodiscard]] std::size_t previous_boundary_byte(std::size_t line_index, std::size_t byte) const;

    private:
        // a run is only ever built through make, because it is shared as an immutable snapshot (§10.5, AD-18)
        explicit ShapedRun(std::vector<Line> lines, float font_size, std::uint64_t font_hash, ShapeSource source);

        // the shaped lines of the run, which never change once the run exists (§10.5)
        std::vector<Line> m_lines;

        // the size every line was shaped at, which a renderer needs to build its font (§9.1)
        float m_font_size = 0.0f;

        // the content hash of the font this run was shaped from, which identifies it across platforms (§15, AD-34)
        std::uint64_t m_font_hash = 0;

        // how this run reaches a surface, which a recorder reads to choose the command it emits (§9.1, AD-34)
        ShapeSource m_source = ShapeSource::Embedded;
    };
} // namespace albor
