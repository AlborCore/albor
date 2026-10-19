#include "render/glyph_outline.h"

#include <utility>

namespace albor
{
    namespace
    {
        // how many outlines the cache keeps, because an unbounded one is a leak an application cannot see (§6.4)
        constexpr std::size_t MAXIMUM_CACHED_OUTLINES = 4096;
    } // namespace

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

    std::shared_ptr<const GlyphOutline> GlyphOutlineCache::lookup(const GlyphOutlineKey& key, const GlyphOutlineResolver& resolver) {
        // a cached outline answers without asking the resolver, because the shape depends on nothing else (§6.4)
        for (const auto& entry : m_outlines) {
            // an exact key match is the same shape, which is what makes a hit correct (§6.4)
            if (entry.first == key)
                return entry.second;
        }
        // a resolver that is not installed builds nothing, which is a refusal rather than an empty glyph (§6.4)
        if (!resolver)
            return nullptr;
        // a miss builds the outline once and then keeps it, because the shape is a pure function of the key (§6.4)
        std::shared_ptr<const GlyphOutline> built = resolver(key);
        // a resolver that refused leaves nothing cached, so a later call can try again (§6.4)
        if (built == nullptr)
            return nullptr;
        // a full cache drops its entries, because an evicted outline rebuilds to exactly the same points (§6.4)
        if (m_outlines.size() >= MAXIMUM_CACHED_OUTLINES)
            m_outlines.clear();
        // the outline joins the cache under the identity it was built for (§6.4)
        m_outlines.emplace_back(key, built);
        // the freshly built outline is what the caller draws (§6.4)
        return built;
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
