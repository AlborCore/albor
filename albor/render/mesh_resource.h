#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "albor/core/geometry.h"
#include "albor/core/paint.h"

namespace albor
{
    // one vertex of a retained triangle mesh, which the backend interpolates across its triangles (§6.4, AD-06)
    struct MeshVertex
    {
        // the position in the current user space, which the context transform places (§6.4)
        Point position{};

        // the normalized texture coordinate the vertex samples, which is ignored without a texture (§6.4)
        Point uv{};

        // the straight alpha colour the sampled texel is multiplied by (§6.4)
        Color color = Color::rgb(1.0f, 1.0f, 1.0f);

        // compare two vertices component wise, which a test asserts on (§6.4)
        friend bool operator==(const MeshVertex& lhs, const MeshVertex& rhs) = default;
    };

    // the immutable triangle mesh a view retains off the ui thread, replayed without rebuilding its vertices (§6.4, AD-06)
    class MeshResource
    {
    public:
        // build a mesh of the given vertices and triangle indices under a factory identifier (§6.4)
        MeshResource(std::uint64_t id, std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices);

        MeshResource(const MeshResource&) = delete;
        MeshResource& operator=(const MeshResource&) = delete;
        MeshResource(MeshResource&&) = delete;
        MeshResource& operator=(MeshResource&&) = delete;
        ~MeshResource() = default;

        // hand out the identifier the factory stamped the mesh with, which keys the backend cache (§6.4, AD-06)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the vertices in index order (§6.4)
        [[nodiscard]] std::span<const MeshVertex> vertices() const;

        // hand out the triangle indices, three per triangle (§6.4)
        [[nodiscard]] std::span<const std::uint32_t> indices() const;

        // hand out how many whole triangles the index list holds (§6.4)
        [[nodiscard]] std::size_t triangle_count() const;

        // report whether the mesh has no triangle to draw (§6.4)
        [[nodiscard]] bool empty() const;

        // report whether the spans are a whole triangle list whose indices all name a vertex (§6.4)
        [[nodiscard]] static bool valid(std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices);

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the vertices in index order (§6.4)
        std::vector<MeshVertex> m_vertices;

        // the triangle indices, three per triangle (§6.4)
        std::vector<std::uint32_t> m_indices;
    };
} // namespace albor
