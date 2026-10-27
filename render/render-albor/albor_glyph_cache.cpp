#include "render/render-albor/albor_glyph_cache.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace albor::render
{
    std::size_t GlyphPathKeyHash::operator()(const GlyphPathKey& key) const noexcept {
        // the content hash is the seed, because it identifies the font the glyph was taken from (§15, AD-34)
        std::size_t hash = std::hash<std::uint64_t>{}(key.font_hash);
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

    std::size_t GlyphMeshKeyHash::operator()(const GlyphMeshKey& key) const noexcept {
        // the outline identity is the seed, because one mesh is one outline expanded (§6.4)
        std::size_t hash = GlyphPathKeyHash{}(key.path);
        // the device scale is mixed in, because the fringe is a device pixel and another scale expands another mesh (AD-27)
        hash ^= std::hash<float>{}(key.device_scale) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        // the mixed value is what the map buckets the key by (§6.4)
        return hash;
    }

    GlyphMeshCache::GlyphMeshCache() :
        // the meshes are aged and bounded by the limits this cache names, which a test asserts on (§6.4, AD-06)
        m_meshes(IDLE_FRAMES, MAXIMUM_CACHED_MESHES) {}

    std::shared_ptr<const GlyphMesh> GlyphMeshCache::find(const GlyphMeshKey& key) {
        // the entry the frame already expanded is handed back for its identity, aged to this frame (§6.4)
        const std::shared_ptr<const GlyphMesh>* found = m_meshes.find(key);
        // a key no frame expanded answers nothing, which is the miss the caller expands for (§6.4)
        return found == nullptr ? nullptr : *found;
    }

    void GlyphMeshCache::store(const GlyphMeshKey& key, std::shared_ptr<const GlyphMesh> mesh) {
        // an absent mesh would take a slot no draw could replay, so it is refused (§6.4)
        if (mesh == nullptr)
            return;
        // the mesh joins under the identity it was expanded for, aged to the frame that expanded it (§6.4)
        m_meshes.store(key, std::move(mesh));
    }

    void GlyphMeshCache::begin_frame(std::uint64_t frame) {
        // the cache is aged at the frame, which drops every mesh no draw reached inside the limit (§6.4, AD-06)
        m_meshes.begin_frame(frame);
    }

    std::size_t GlyphMeshCache::count() const {
        // the count is what a test asserts on, because a cache is otherwise invisible (§6.4)
        return m_meshes.count();
    }
} // namespace albor::render
