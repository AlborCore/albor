#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "render/aged_cache.h"
#include "render/render-albor/albor_glyph_path.h"

namespace albor::render
{
    // the identity one walked glyph path answers to, which is the font content, the glyph and the size (§6.4)
    struct GlyphPathKey
    {
        // the content hash of the font, which two engines over the same bytes answer alike (§15, AD-34)
        std::uint64_t font_hash = 0;

        // the glyph index inside that typeface, which is what a shaped run carries (§9.1)
        std::uint32_t glyph_id = 0;

        // the size the outline was walked at, because a path does not scale (§9.1, AD-27)
        float font_size = 0.0f;

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
        // how many frames an unreferenced path survives, which bounds the memory a long run holds (AD-06)
        static constexpr std::uint64_t IDLE_FRAMES = 120;

        // how many paths a cache aims to hold, which one frame's own working set may briefly exceed (AD-06)
        static constexpr std::size_t MAXIMUM_CACHED_PATHS = 8192;

        // build the cache every surface keeps, which ages its paths by the frame the surface advances (AD-06)
        GlyphPathCache();

        // hand out the path a key was walked to, or null when no frame has walked it (§6.4)
        [[nodiscard]] std::shared_ptr<const GlyphPath> find(const GlyphPathKey& key);

        // keep a walked path under the identity it was walked for, so a later frame draws it without walking (§6.4)
        void store(const GlyphPathKey& key, std::shared_ptr<const GlyphPath> path);

        // age the cache at a frame, dropping the paths no draw reached within the idle limit (§6.4, AD-06)
        void begin_frame(std::uint64_t frame);

        // report how many paths the cache holds, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t count() const;

    private:
        // the paths keyed by the identity they were walked for, aged by the frame that last drew them (§6.4, AD-06)
        AgedCache<GlyphPathKey, std::shared_ptr<const GlyphPath>, GlyphPathKeyHash> m_paths;
    };
} // namespace albor::render
