#include "render/render-albor/albor_glyph_cache.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace albor::render
{
    std::size_t GlyphPathKeyHash::operator()(const GlyphPathKey& key) const noexcept {
        // the typeface is the seed, because it identifies the font the glyph was taken from (§9.1)
        std::size_t hash = std::hash<const void*>{}(key.font);
        // the glyph index is mixed in, because two glyphs of one typeface are two different shapes (§6.4)
        hash ^= std::hash<std::uint32_t>{}(key.glyph_id) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        // the quantised size is mixed in, because one glyph at two sizes is two different shapes (§9.1, AD-27)
        hash ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.font_size * 64.0f)) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        // the mixed value is what the map buckets the key by (§6.4)
        return hash;
    }

    GlyphPathCache::GlyphPathCache() :
        // the paths are aged and bounded by the limits this cache names, which a test asserts on (§6.4, AD-06)
        m_paths(IDLE_FRAMES, MAXIMUM_CACHED_PATHS) {}

    std::shared_ptr<const GlyphPath> GlyphPathCache::find(const GlyphPathKey& key) {
        // the entry the frame already walked is handed back for its identity, aged to this frame (§6.4)
        const std::shared_ptr<const GlyphPath>* found = m_paths.find(key);
        // a key no frame walked answers nothing, which is the miss the caller walks for (§6.4)
        return found == nullptr ? nullptr : *found;
    }

    void GlyphPathCache::store(const GlyphPathKey& key, std::shared_ptr<const GlyphPath> path) {
        // the path joins under the identity it was walked for, aged to the frame that walked it (§6.4)
        m_paths.store(key, std::move(path));
    }

    void GlyphPathCache::begin_frame(std::uint64_t frame) {
        // the cache is aged at the frame, which drops every path no draw reached inside the limit (§6.4, AD-06)
        m_paths.begin_frame(frame);
    }

    std::size_t GlyphPathCache::count() const {
        // the count is what a test asserts on, because a cache is otherwise invisible (§6.4)
        return m_paths.count();
    }
} // namespace albor::render
