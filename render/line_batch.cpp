#include "render/line_batch.h"

namespace albor
{
    LineBatch::LineBatch(std::uint64_t id, std::span<const Point> points) :
        m_id(id), m_points(points.begin(), points.end()) {}

    std::uint64_t LineBatch::id() const {
        // the identifier is what the factory stamped at creation (§6.4, AD-06)
        return m_id;
    }

    std::span<const Point> LineBatch::points() const {
        // the span covers every stored endpoint, which the backend walks per segment (§6.4)
        return m_points;
    }

    std::size_t LineBatch::point_count() const {
        // the count is the raw endpoint count (§6.4)
        return m_points.size();
    }

    std::size_t LineBatch::segment_count() const {
        // a segment consumes two endpoints, so a dangling half point is not a segment (§6.4)
        return m_points.size() / 2;
    }
} // namespace albor
