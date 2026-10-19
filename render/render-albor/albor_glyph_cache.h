#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "render/render-albor/albor_glyph_path.h"

namespace albor::render
{
    // the identity one walked glyph path answers to, which is the typeface, the glyph, the size and the era (§6.4, AD-06)
    struct GlyphPathKey
    {
        // the wrapped typeface, which the engine that issued the handle keeps alive for as long as it lives (§9.1)
        const void* font = nullptr;

        // the glyph index inside that typeface, which is what a shaped run carries (§9.1)
        std::uint32_t glyph_id = 0;

        // the size the outline was walked at, because a path does not scale (§9.1, AD-27)
        float font_size = 0.0f;

        // the era the typeface was named in, which drops every key when an engine dies with its typefaces (§6.4, AD-06)
        std::uint64_t era = FontHandle::era();

        // compare two keys component wise, which an exact match needs (§6.4)
        friend bool operator==(const GlyphPathKey& lhs, const GlyphPathKey& rhs) = default;
    };

    // the hash a key carries, which mixes its three fields into the bucket the map reads (§6.4)
    struct GlyphPathKeyHash
    {
        // hash one key, which is what the map buckets its entries by (§6.4)
        [[nodiscard]] std::size_t operator()(const GlyphPathKey& key) const noexcept;
    };

    // the paths a surface already walked, which keeps a frame from walking the same glyph again (§6.4, AD-06)
    class GlyphPathCache
    {
    public:
        // how many frames an unreferenced path stays cached, which bounds a long run's memory (AD-06)
        static constexpr std::uint64_t IDLE_FRAMES = 120;

        // how many paths one cache holds before it drops them, because an unbounded cache is a leak (AD-06)
        static constexpr std::size_t MAXIMUM_CACHED_PATHS = 8192;
        // hand out the path a key was walked to, or null when no frame has walked it (§6.4)
        [[nodiscard]] std::shared_ptr<const GlyphPath> find(const GlyphPathKey& key);

        // keep a walked path under the identity it was walked for, so a later frame draws it without walking (§6.4)
        void store(const GlyphPathKey& key, std::shared_ptr<const GlyphPath> path);

        // age the cache at a frame, dropping the paths no draw reached within the idle limit (§6.4, AD-06)
        void begin_frame(std::uint64_t frame);

        // report how many paths the cache holds, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t count() const;

    private:
        // drop the coldest slice of the full cache, because a rebuilt path is the same shape but not free (§6.4, AD-06)
        void drop_the_coldest_slice();

        // one cached path with the frame it was last reached in, which the sweep reads (§6.4, AD-06)
        struct Entry
        {
            // the walked outline, retained so a frame replays it without walking the typeface again (§6.4)
            std::shared_ptr<const GlyphPath> path;

            // the frame the path was last found or stored in, which the sweep ages against (§6.4)
            std::uint64_t last_used = 0;
        };

        // the paths keyed by the identity they were walked for (§6.4)
        std::unordered_map<GlyphPathKey, Entry, GlyphPathKeyHash> m_paths;

        // the frame the cache is being aged at, which a find or a store stamps its entry with (§6.4, AD-06)
        std::uint64_t m_frame = 0;
    };
} // namespace albor::render
