#include "render/glyph_outline.h"

#include <utility>

namespace albor
{
    GlyphOutline::GlyphOutline(std::uint64_t font_hash, std::uint32_t glyph_id, float font_size, GlyphOutlineData outline) :
        m_font_hash(font_hash), m_glyph_id(glyph_id), m_font_size(font_size), m_outline(std::move(outline)) {
        // a stream whose verbs demand a different number of points cannot be replayed, so it is refused here (§6.4)
        if (PathResource::point_demand(std::span<const PathVerb>(m_outline.verbs)) != m_outline.points.size())
            m_outline = GlyphOutlineData{};
        // the identifier starts at the font hash, because every glyph of one font shares that identity (§15, AD-34)
        std::uint64_t identifier = font_hash;
        // the glyph index is mixed in, because two glyphs of one font are different shapes (§6.4)
        identifier = identifier * 1099511628211ull ^ static_cast<std::uint64_t>(glyph_id);
        // the quantised size is mixed in, because one glyph at two sizes is two different shapes (§6.4, AD-27)
        identifier = identifier * 1099511628211ull ^ static_cast<std::uint64_t>(font_size * 64.0f);
        // the path is retained once, so a frame replays it without allocating a resource per glyph (§6.4, AD-06)
        m_path = std::make_shared<const PathResource>(identifier, std::span<const PathVerb>(m_outline.verbs), std::span<const Point>(m_outline.points));
    }

    std::uint64_t GlyphOutline::font_hash() const {
        // the hash is what the replay resolves the font by, because a name would resolve per platform (§15, AD-34)
        return m_font_hash;
    }

    std::uint32_t GlyphOutline::glyph_id() const {
        // the glyph index is what a caller reports a missing outline against (§9.1)
        return m_glyph_id;
    }

    float GlyphOutline::font_size() const {
        // the size is recorded because an outline is in pixels and does not scale afterwards (§9.1, AD-27)
        return m_font_size;
    }

    const std::vector<Point>& GlyphOutline::points() const {
        // the points are what every contour in the stream consumes, which the replay walks once (§6.4)
        return m_outline.points;
    }

    const std::vector<PathVerb>& GlyphOutline::verbs() const {
        // the verbs pair against the points and keep every contour apart (§6.4)
        return m_outline.verbs;
    }

    const std::shared_ptr<const PathResource>& GlyphOutline::path() const {
        // the retained path is what a frame draws, so the replay allocates nothing per glyph (§6.4, AD-06)
        return m_path;
    }

    bool GlyphOutline::empty() const {
        // a glyph with no points draws nothing, which is what a space is (§6.2)
        return m_outline.points.empty();
    }

    std::size_t GlyphOutlineKeyHash::operator()(const GlyphOutlineKey& key) const noexcept {
        // the font hash is the seed, because it identifies the font the glyph was taken from (§15, AD-34)
        std::size_t hash = std::hash<std::uint64_t>{}(key.font_hash);
        // the glyph index is mixed in, because two glyphs of one font are two different shapes (§6.4)
        hash ^= std::hash<std::uint32_t>{}(key.glyph_id) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        // the quantised size is mixed in, because one glyph at two sizes is two different shapes (§6.4, AD-27)
        hash ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.font_size * 64.0f)) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        // the mixed value is what the map buckets the key by (§6.4)
        return hash;
    }

    std::shared_ptr<const GlyphOutline> GlyphOutlineCache::lookup(const GlyphOutlineKey& key, const GlyphOutlineResolver& resolver) {
        // the entry is read by the exact identity of the shape, which a map answers without scanning (§6.4)
        const auto found = m_outlines.find(key);
        // a cached outline answers without asking the resolver, because the shape depends on nothing else (§6.4)
        if (found != m_outlines.end())
            return found->second;
        // a resolver that is not installed builds nothing, which is a refusal rather than an empty glyph (§6.4)
        if (!resolver)
            return nullptr;
        // a miss builds the outline once, because the shape is a pure function of the key (§6.4)
        std::shared_ptr<const GlyphOutline> built = resolver(key);
        // a resolver that refused leaves nothing cached, so a later call can try again (§6.4)
        if (built == nullptr)
            return nullptr;
        // a full cache drops a slice before the new outline joins, because a rebuilt outline is not free (§6.4, AD-06)
        if (m_outlines.size() >= MAXIMUM_CACHED_OUTLINES)
            drop_a_slice();
        // the outline joins the cache under its identity (§6.4)
        return m_outlines.emplace(key, std::move(built)).first->second;
    }

    void GlyphOutlineCache::drop_a_slice() {
        // the slice is a fixed fraction, because dropping the whole cache is too expensive (§6.4, AD-06)
        const std::size_t slice = MAXIMUM_CACHED_OUTLINES / 8u;
        // the entries go one at a time, because a map hands out no window a caller could erase (§6.4)
        std::size_t dropped = 0u;
        for (auto entry = m_outlines.begin(); entry != m_outlines.end() && dropped < slice;) {
            // the entry is erased here rather than cleared away, because the entries beyond the slice must stay (§6.4)
            entry = m_outlines.erase(entry);
            // the count is what stops the loop once the slice is gone (§6.4)
            dropped++;
        }
    }

    void GlyphOutlineCache::clear() {
        // the outlines are dropped, so one test's cache cannot answer another's (§2.2)
        m_outlines.clear();
    }

    std::size_t GlyphOutlineCache::count() const {
        // the count is what a test asserts on, because a cache is otherwise invisible (§6.4)
        return m_outlines.size();
    }
} // namespace albor
