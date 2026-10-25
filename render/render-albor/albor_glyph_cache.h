#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "render/aged_cache.h"
#include "render/render-albor/albor_batch.h"
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

    // the expanded fill of one glyph at one device scale, which a later frame translates instead of sweeping (§6.4, AD-06)
    struct GlyphMesh
    {
        // the fill and fringe vertices around the glyph origin, already in device space at the scale the key names
        std::vector<BatchVertex> vertices;

        // the triangle indices into those vertices, which a frame places through its own vertex base
        std::vector<std::uint32_t> indices;
    };

    // the identity one expanded glyph answers to, which is its outline and the device scale (§6.4, AD-27)
    struct GlyphMeshKey
    {
        // the outline the mesh expands, which is the identity the path cache already walks under (§6.4)
        GlyphPathKey path;

        // the device scale the fill and fringe land at, because the fringe is a device pixel wide (AD-27)
        float device_scale = 1.0f;

        // compare two keys component wise, which an exact match needs (§6.4)
        friend bool operator==(const GlyphMeshKey& lhs, const GlyphMeshKey& rhs) = default;
    };

    // the hash a mesh key carries, which mixes its outline identity and its device scale (§6.4)
    struct GlyphMeshKeyHash
    {
        // hash one key, which is what the map buckets its entries by (§6.4)
        [[nodiscard]] std::size_t operator()(const GlyphMeshKey& key) const noexcept;
    };

    // the expanded glyphs a surface retains, which keeps a frame from sweeping the same outline again (§6.4, AD-06)
    class GlyphMeshCache
    {
    public:
        // how many frames an unreferenced mesh survives, which matches the outline cache beside it (AD-06)
        static constexpr std::uint64_t IDLE_FRAMES = 120;

        // how many meshes a cache aims to hold, which bounds the device space geometry a window retains (AD-06)
        static constexpr std::size_t MAXIMUM_CACHED_MESHES = 2048;

        // build the cache every surface keeps, which ages its meshes by the frame the surface advances (AD-06)
        GlyphMeshCache();

        // hand out the mesh a key was expanded to, or null when no frame has expanded it (§6.4)
        [[nodiscard]] std::shared_ptr<const GlyphMesh> find(const GlyphMeshKey& key);

        // keep an expanded mesh under the identity it was built for, so a later frame replays it (§6.4, AD-06)
        void store(const GlyphMeshKey& key, std::shared_ptr<const GlyphMesh> mesh);

        // age the cache at a frame, dropping the meshes no draw reached within the idle limit (§6.4, AD-06)
        void begin_frame(std::uint64_t frame);

        // report how many meshes the cache holds, which a test asserts on (§6.4)
        [[nodiscard]] std::size_t count() const;

    private:
        // the meshes keyed by the outline and scale they were expanded for, aged by the frame that last drew them (§6.4)
        AgedCache<GlyphMeshKey, std::shared_ptr<const GlyphMesh>, GlyphMeshKeyHash> m_meshes;
    };
} // namespace albor::render
