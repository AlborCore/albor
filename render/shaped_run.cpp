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

    std::size_t ShapedRun::line_at_byte(std::size_t byte) const {
        // an empty run shaped no line, so the first index stands for the absence of one (§9.1)
        if (m_lines.empty())
            return 0;
        // the lines are in reading order, so the first line ending past the offset is the one holding it (§9.1)
        for (std::size_t index = 0; index < m_lines.size(); index++) {
            // a line that shaped nothing owns the byte it begins at, because a blank row is a caret stop (§9.1)
            if (m_lines[index].glyphs.empty() && m_lines[index].start_byte == m_lines[index].end_byte && byte == m_lines[index].start_byte)
                return index;
            if (byte < m_lines[index].end_byte)
                return index;
        }
        // an offset past every line resolves to the last, because a run never reports a line it did not shape (§9.1)
        return m_lines.size() - 1;
    }

    float ShapedRun::x_at_byte(std::size_t line_index, std::size_t byte) const {
        // a line the run does not carry has no position, so the offset reported is the origin (§9.1)
        if (line_index >= m_lines.size())
            return 0.0f;
        // the line is resolved once, because every answer reads the same glyphs (§9.1)
        const Line& line = m_lines[line_index];
        // a line with no glyphs puts its caret at the origin, which is what an empty line means (§9.1)
        if (line.glyphs.empty())
            return 0.0f;
        // the first glyph at or past the offset is where a caret stands before that glyph (§9.1)
        for (const ShapedGlyph& glyph : line.glyphs)
            if (glyph.cluster >= byte)
                return glyph.position.x;
        // an offset past every glyph sits at the end of the line's advance (§9.1)
        return line.width;
    }

    std::size_t ShapedRun::byte_at_x(std::size_t line_index, float x) const {
        // a line the run does not carry has no offset, so the start of the text stands (§9.1)
        if (line_index >= m_lines.size())
            return 0;
        // the line is resolved once, because every answer reads the same glyphs (§9.1)
        const Line& line = m_lines[line_index];
        // a line with no glyphs resolves to the offset it begins at (§9.1)
        if (line.glyphs.empty())
            return line.start_byte;
        // a position left of the first glyph resolves to the offset that glyph carries (§9.1)
        if (x <= line.glyphs.front().position.x)
            return line.glyphs.front().cluster;
        // a position right of the advance resolves to the end of the line (§9.1)
        if (x >= line.width)
            return line.end_byte;
        // otherwise the last glyph the position has passed wins, which snaps a caret to the boundary before it (§9.1)
        std::size_t nearest = line.glyphs.front().cluster;
        for (const ShapedGlyph& glyph : line.glyphs) {
            // a glyph the position has not reached ends the search, because pen positions rise in logical order (§9.1)
            if (glyph.position.x > x)
                break;
            // a glyph the position has passed becomes the candidate the caret snaps back to (§9.1)
            nearest = glyph.cluster;
        }
        return nearest;
    }

    std::size_t ShapedRun::next_boundary_byte(std::size_t line_index, std::size_t byte) const {
        // a line the run does not carry has no boundary to step to, so the offset stands (§9.1)
        if (line_index >= m_lines.size())
            return byte;
        // the line is resolved once, because every answer reads the same glyphs (§9.1)
        const Line& line = m_lines[line_index];
        // a step starting at or past the end of the line has nothing to reach, so it stays (§9.1)
        if (byte >= line.end_byte)
            return line.end_byte;
        // the offset the step lands on starts at the end of the line, which is where the last glyph steps to (§9.1)
        std::size_t next = line.end_byte;
        // the nearest cluster past the offset is the next character, because a cluster is one character (§9.1)
        for (const ShapedGlyph& glyph : line.glyphs)
            if (glyph.cluster > byte && glyph.cluster < next)
                next = glyph.cluster;
        // the nearest cluster is where the caret lands (§9.1)
        return next;
    }

    std::size_t ShapedRun::previous_boundary_byte(std::size_t line_index, std::size_t byte) const {
        // a line the run does not carry has no boundary to step back to, so the offset stands (§9.1)
        if (line_index >= m_lines.size())
            return byte;
        // the line is resolved once, because every answer reads the same glyphs (§9.1)
        const Line& line = m_lines[line_index];
        // a step from at or before the start of the line has nothing to reach, so it stays (§9.1)
        if (byte <= line.start_byte)
            return line.start_byte;
        // the offset the step lands on starts at the beginning of the line (§9.1)
        std::size_t previous = line.start_byte;
        // the furthest cluster short of the offset is the previous character, because a cluster is one (§9.1)
        for (const ShapedGlyph& glyph : line.glyphs)
            if (glyph.cluster < byte && glyph.cluster > previous)
                previous = glyph.cluster;
        // the furthest cluster is where the caret lands (§9.1)
        return previous;
    }
} // namespace albor
