#include "albor/render/mesh_resource.h"

namespace albor
{
    MeshResource::MeshResource(std::uint64_t id, std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices) :
        m_id(id), m_vertices(vertices.begin(), vertices.end()), m_indices(indices.begin(), indices.end()) {}

    std::uint64_t MeshResource::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    std::span<const MeshVertex> MeshResource::vertices() const {
        // the vertices are handed back in index order (§6.4)
        return m_vertices;
    }

    std::span<const std::uint32_t> MeshResource::indices() const {
        // the indices are handed back in drawing order, three per triangle (§6.4)
        return m_indices;
    }

    std::size_t MeshResource::triangle_count() const {
        // a triangle consumes three indices, so a trailing partial triangle is not one (§6.4)
        return m_indices.size() / 3;
    }

    bool MeshResource::empty() const {
        // a mesh with no index has no triangle to draw, whatever vertices it carries (§6.4)
        return m_indices.empty();
    }

    bool MeshResource::valid(std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices) {
        // a mesh without a vertex has nowhere to place a triangle, so it is refused (§6.4)
        if (vertices.empty())
            return false;
        // a mesh without an index draws no triangle, so it is refused (§6.4)
        if (indices.empty())
            return false;
        // an index list that is not a whole triangle list is malformed, so it is refused (§6.4)
        if (indices.size() % 3 != 0)
            return false;
        // every index must name a vertex, because a triangle reads all three (§6.4)
        for (const std::uint32_t index : indices) {
            // an index at or past the vertex count has no vertex to read (§6.4)
            if (index >= vertices.size())
                return false;
        }
        // every check passed, so the spans form a whole triangle list (§6.4)
        return true;
    }
} // namespace albor
