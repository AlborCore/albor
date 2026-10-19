#include "render/render-albor/albor_glyph_cache.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

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

    std::shared_ptr<const GlyphPath> GlyphPathCache::find(const GlyphPathKey& key) {
        // the entry is looked up by the exact identity the path was walked for (§6.4)
        const auto found = m_paths.find(key);
        // a key nothing was stored under answers nothing, which is the miss the caller walks for (§6.4)
        if (found == m_paths.end())
            return nullptr;
        // the hit ages its entry to this frame, which is what keeps it from the sweep (§6.4, AD-06)
        found->second.last_used = m_frame;
        // the walked outline is handed back, which the frame draws without walking the typeface again (§6.4)
        return found->second.path;
    }

    void GlyphPathCache::store(const GlyphPathKey& key, std::shared_ptr<const GlyphPath> path) {
        // a full cache drops its coldest slice, because a rebuilt path is the same shape but not free (§6.4, AD-06)
        if (m_paths.size() >= MAXIMUM_CACHED_PATHS)
            drop_the_coldest_slice();
        // the path joins under the identity it was walked for, aged to the frame that walked it (§6.4)
        m_paths[key] = Entry{.path = std::move(path), .last_used = m_frame};
    }

    void GlyphPathCache::drop_the_coldest_slice() {
        // the slice is a fixed fraction, because dropping the whole cache re-walked every glyph of a frame (§6.4, AD-06)
        const std::size_t slice = MAXIMUM_CACHED_PATHS / 8u;
        // every entry's age is gathered with its identity, which is what the coldest slice is chosen from (§6.4, AD-06)
        std::vector<std::pair<std::uint64_t, GlyphPathKey>> ages;
        ages.reserve(m_paths.size());
        for (const auto& [key, entry] : m_paths)
            ages.emplace_back(entry.last_used, key);
        // the coldest slice is sorted to the front, because the entries no draw reached soonest are the ones to go (§6.4)
        std::partial_sort(ages.begin(), ages.begin() + static_cast<std::ptrdiff_t>(std::min(slice, ages.size())), ages.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        // the slice is erased by identity, because a map hands out no window a caller could erase (§6.4)
        for (std::size_t index = 0u; index < slice; index++)
            m_paths.erase(ages[index].second);
    }

    void GlyphPathCache::begin_frame(std::uint64_t frame) {
        // the frame is what every entry ages against, which the sweep below compares to (§6.4, AD-06)
        m_frame = frame;
        // the sweep walks every entry, because a path no draw reached is memory a frame cannot use (§6.4, AD-06)
        for (auto entry = m_paths.begin(); entry != m_paths.end();) {
            // an entry reached within the idle limit stays, which is what a repeating frame needs (§6.4)
            const bool fresh = m_frame - entry->second.last_used <= IDLE_FRAMES;
            // a fresh entry is passed over, and a stale one is erased here rather than later (§6.4, AD-06)
            entry = fresh ? std::next(entry) : m_paths.erase(entry);
        }
    }

    std::size_t GlyphPathCache::count() const {
        // the count is what a test asserts on, because a cache is otherwise invisible (§6.4)
        return m_paths.size();
    }
} // namespace albor::render
