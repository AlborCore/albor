#include "render/glyph_outline.h"

#include <utility>

namespace albor
{
    namespace
    {
        // how many outlines the cache keeps, because an unbounded one is a leak an application cannot see (§6.4)
        constexpr std::size_t MAXIMUM_CACHED_OUTLINES = 4096;
    } // namespace

    GlyphOutline::GlyphOutline(std::uint64_t font_hash, std::uint32_t glyph_id, float font_size, std::vector<Point> points) :
        m_font_hash(font_hash), m_glyph_id(glyph_id), m_font_size(font_size), m_points(std::move(points)) {}

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
        // the points are the tessellated contour, which the replay walks once (§6.4)
        return m_points;
    }

    bool GlyphOutline::empty() const {
        // a glyph with no points draws nothing, which is what a space is (§6.2)
        return m_points.empty();
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
