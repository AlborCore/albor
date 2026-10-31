#include "render/path_resource.h"

#include <limits>

namespace albor
{
    PathResource::PathResource(std::uint64_t id, std::span<const PathVerb> verbs, std::span<const Point> points) :
        m_id(id), m_verbs(verbs.begin(), verbs.end()), m_points(points.begin(), points.end()) {}

    std::uint64_t PathResource::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    std::span<const PathVerb> PathResource::verbs() const {
        // the stream is handed back in drawing order (§6.4)
        return m_verbs;
    }

    std::span<const Point> PathResource::points() const {
        // the points are handed back in the order the verbs consume them (§6.4)
        return m_points;
    }

    std::size_t PathResource::point_demand(std::span<const PathVerb> verbs) {
        // the running count starts at no demand (§6.4)
        std::size_t demand = 0;
        // every verb adds what it consumes, so a mismatching point list is detectable (§6.4)
        for (const PathVerb verb : verbs) {
            // a move and a line each take the next point (§6.4)
            if (verb == PathVerb::MoveTo || verb == PathVerb::LineTo)
                demand++;
            // a quadratic consumes one control point and one end point (§6.4)
            else if (verb == PathVerb::QuadraticTo)
                demand += 2;
            // a cubic consumes two control points and one end point (§6.4)
            else if (verb == PathVerb::CubicTo)
                demand += 3;
            // a close takes no point (§6.4)
            else if (verb == PathVerb::Close)
                continue;
            // an unknown verb cannot be satisfied, which the largest size_t marks (§6.4)
            else
                return std::numeric_limits<std::size_t>::max();
        }
        // hand back the total the stream consumes (§6.4)
        return demand;
    }
} // namespace albor
