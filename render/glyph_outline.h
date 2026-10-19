#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/geometry.h"
#include "render/path_resource.h"

namespace albor
{
    // one glyph's tessellated outline, whose verbs open and seal a contour over the points they consume (§6.4)
    struct GlyphOutlineData
    {
        // the verbs in drawing order, where a MoveTo opens a contour and a Close seals it (§6.4)
        std::vector<PathVerb> verbs;

        // the points the verbs consume in order, already in logical pixels and y down from the baseline (§6.4, AD-27)
        std::vector<Point> points;

        // compare two outlines component wise, which the outline's own comparison reads (§6.4)
        friend bool operator==(const GlyphOutlineData& lhs, const GlyphOutlineData& rhs) = default;
    };

    // one glyph's outline, tessellated from the embedded font and retained so a frame does not rebuild it (§6.4, AD-06)
    class GlyphOutline
    {
    public:
        // build an outline over the glyph's own contours, which are already in logical pixels (§6.4, AD-27)
        GlyphOutline(std::uint64_t font_hash, std::uint32_t glyph_id, float font_size, GlyphOutlineData outline);

        GlyphOutline(const GlyphOutline&) = delete;
        GlyphOutline& operator=(const GlyphOutline&) = delete;
        GlyphOutline(GlyphOutline&&) = delete;
        GlyphOutline& operator=(GlyphOutline&&) = delete;
        ~GlyphOutline() = default;

        // the content hash of the font this glyph came from, which is what the replay resolves it against (§15, AD-34)
        [[nodiscard]] std::uint64_t font_hash() const;

        // the glyph index inside that font, which is what a caller reports a failure about (§9.1)
        [[nodiscard]] std::uint32_t glyph_id() const;

        // the size the glyph was tessellated at, because an outline is in pixels and does not scale (§9.1, AD-27)
        [[nodiscard]] float font_size() const;

        // the outline's points, which every contour in the verb stream consumes in order (§6.4)
        [[nodiscard]] const std::vector<Point>& points() const;

        // the verbs paired with those points, one contour per move and close pair (§6.4)
        [[nodiscard]] const std::vector<PathVerb>& verbs() const;

        // the retained path built once over the points, which a frame draws without allocating (§6.4, AD-06)
        [[nodiscard]] const std::shared_ptr<const PathResource>& path() const;

        // report whether the glyph has no outline, which is what a space is (§6.2)
        [[nodiscard]] bool empty() const;

        // compare two outlines field by field, which a caller comparing two shapes reads (§6.4)
        friend bool operator==(const GlyphOutline& lhs, const GlyphOutline& rhs) = default;

    private:
        // the font this glyph came from, which is the identity the replay resolves (§15, AD-34)
        std::uint64_t m_font_hash = 0;

        // the glyph index inside that font (§9.1)
        std::uint32_t m_glyph_id = 0;

        // the size the points were tessellated at (§9.1, AD-27)
        float m_font_size = 0.0f;

        // the tessellated contours, which are empty for a glyph that draws nothing (§6.2)
        GlyphOutlineData m_outline;

        // the path built once over the points and verbs, so a frame replays without allocating (§6.4, AD-06)
        std::shared_ptr<const PathResource> m_path;
    };

    // the identity a glyph outline is cached under, which is everything that changes the shape (§6.4)
    struct GlyphOutlineKey
    {
        // the content hash of the font, because two fonts of one family are different shapes (§15, AD-34)
        std::uint64_t font_hash = 0;

        // the glyph index inside that font (§9.1)
        std::uint32_t glyph_id = 0;

        // the size the outline is tessellated at, because an outline does not scale (§9.1, AD-27)
        float font_size = 0.0f;

        // compare two keys component wise, which is what a hash map needs (§6.4)
        friend bool operator==(const GlyphOutlineKey& lhs, const GlyphOutlineKey& rhs) = default;
    };

    // hand out the outline for a key, or null when no font of that hash is registered (§6.4, §15)
    using GlyphOutlineResolver = std::function<std::shared_ptr<const GlyphOutline>(const GlyphOutlineKey&)>;

    // the cache of tessellated outlines, which is what keeps a frame from rebuilding them (§6.4, AD-06)
    class GlyphOutlineCache
    {
    public:
        // hand out the outline for a key, building it through the resolver on a miss (§6.4)
        [[nodiscard]] std::shared_ptr<const GlyphOutline> lookup(const GlyphOutlineKey& key, const GlyphOutlineResolver& resolver);

        // empty the cache, which a test does so that one test's outlines cannot answer another's (§2.2)
        void clear();

        // report how many outlines are cached, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t count() const;

    private:
        // the outlines kept, keyed by the identity of the shape they hold (§6.4)
        std::vector<std::pair<GlyphOutlineKey, std::shared_ptr<const GlyphOutline>>> m_outlines;
    };
} // namespace albor
