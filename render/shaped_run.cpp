#include "render/shaped_run.h"

#include <utility>

namespace albor
{
    std::shared_ptr<const ShapedRun> ShapedRun::make(std::vector<Line> lines, float font_size, std::uint64_t font_hash) {
        // a run is shared as an immutable snapshot, because the render thread only ever reads it (§10.5, AD-18)
        return std::shared_ptr<const ShapedRun>(new ShapedRun(std::move(lines), font_size, font_hash));
    }

    ShapedRun::ShapedRun(std::vector<Line> lines, float font_size, std::uint64_t font_hash) :
        m_lines(std::move(lines)), m_font_size(font_size), m_font_hash(font_hash) {}

    float ShapedRun::font_size() const {
        // the size is what every line of the run was shaped at (§9.1)
        return m_font_size;
    }

    std::uint64_t ShapedRun::font_hash() const {
        // the hash is the font's identity, and zero means the engine reported none (§15, AD-34)
        return m_font_hash;
    }

    const std::vector<ShapedRun::Line>& ShapedRun::lines() const {
        // the lines are in reading order, so a renderer replays them as they come (§9.1)
        return m_lines;
    }

    bool ShapedRun::empty() const {
        // a run whose lines carry no glyph costs nothing to draw (§6.2)
        for (const Line& line : m_lines)
            if (!line.glyphs.empty())
                return false;
        return true;
    }

    std::size_t ShapedRun::glyph_count() const {
        // the count sums the lines, which is what a caller asks to size a reservation with (§9.1)
        std::size_t count = 0;
        for (const Line& line : m_lines)
            count += line.glyphs.size();
        return count;
    }
} // namespace albor
