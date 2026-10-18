#include "render/render-albor/albor_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace albor::render
{
    namespace
    {
        // the miter limit the fill fringe joins with, which the nanovg technique fixes (§6.2)
        constexpr float FILL_MITER_LIMIT = 2.4f;

        // the half circle constant the caps and the joins sweep with
        constexpr float PI = 3.14159265358979f;

        // one vertex of a strip sequence, which the emitter appends verbatim
        struct StripVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float lateral = 0.0f;
            float longitudinal = 1.0f;
        };

        // append one vertex and hand back its index
        std::uint32_t emit_vertex(FrameBatch& sink, float x, float y, float lateral, float longitudinal) {
            // the vertex joins the frame mesh
            BatchVertex vertex;
            vertex.x = x;
            vertex.y = y;
            vertex.lateral = lateral;
            vertex.longitudinal = longitudinal;
            sink.vertices.push_back(vertex);
            return static_cast<std::uint32_t>(sink.vertices.size() - 1);
        }

        // append one triangle over three already emitted vertices
        void emit_triangle(FrameBatch& sink, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            sink.indices.push_back(a);
            sink.indices.push_back(b);
            sink.indices.push_back(c);
        }

        // append the strip triangles over a run of vertices that starts at the given index
        void emit_strip(FrameBatch& sink, std::uint32_t first, std::size_t count) {
            // a strip of n vertices carries n minus two triangles, which a shorter run cannot form
            for (std::size_t index = 0; index + 2 < count; index++) {
                // every other triangle swaps its base, which is the winding a strip alternates (§6.3)
                if (index % 2 == 0)
                    emit_triangle(sink, static_cast<std::uint32_t>(first + index), static_cast<std::uint32_t>(first + index + 1), static_cast<std::uint32_t>(first + index + 2));
                else
                    emit_triangle(sink, static_cast<std::uint32_t>(first + index + 1), static_cast<std::uint32_t>(first + index), static_cast<std::uint32_t>(first + index + 2));
            }
        }

        // append a sequence of strip vertices without indices, which the stroke strip the caller spans stitches
        void append_vertices(FrameBatch& sink, const std::vector<StripVertex>& sequence) {
            // the vertices join the mesh in the order the sequence carries
            for (const StripVertex& vertex : sequence)
                emit_vertex(sink, vertex.x, vertex.y, vertex.lateral, vertex.longitudinal);
        }

        // report whether the corner is convex, which the positive area winding in the y down space makes a negative cross
        bool turns_left(Point previous, Point point, Point next) {
            // the cross of the two edges decides the turn, which the winding already fixed
            return (point.x - previous.x) * (next.y - point.y) - (point.y - previous.y) * (next.x - point.x) < 0.0f;
        }

        // report whether a point sits inside a triangle, which the ear test refuses
        bool inside_triangle(Point point, Point a, Point b, Point c) {
            // the three edge signs decide the containment, which a point on an edge still counts for
            const float ab = (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
            const float bc = (c.x - b.x) * (point.y - b.y) - (c.y - b.y) * (point.x - b.x);
            const float ca = (a.x - c.x) * (point.y - c.y) - (a.y - c.y) * (point.x - c.x);
            return ab <= 0.0f && bc <= 0.0f && ca <= 0.0f;
        }

        // triangulate a simple ring, which a concave contour cannot fan (ear clipping technique)
        std::vector<std::array<std::size_t, 3>> ear_clip(const std::vector<Point>& polygon) {
            // the triangles the clip produces, in the order it removes the ears
            std::vector<std::array<std::size_t, 3>> triangles;
            // the ring vertices still to clip, which the walk removes one at a time
            std::vector<std::size_t> remaining;
            for (std::size_t index = 0; index < polygon.size(); index++)
                remaining.push_back(index);
            // a ring of n points carries n minus two triangles, which the loop emits
            while (remaining.size() > 3) {
                // the ear the pass found, which no later iteration needs to revisit
                bool clipped = false;
                for (std::size_t at = 0; at < remaining.size(); at++) {
                    // the three points of the candidate ear, which wraps around the ring
                    const std::size_t previous = remaining[(at + remaining.size() - 1) % remaining.size()];
                    const std::size_t point = remaining[at];
                    const std::size_t next = remaining[(at + 1) % remaining.size()];
                    // a reflex corner is no ear, which the left turn test refuses
                    if (!turns_left(polygon[previous], polygon[point], polygon[next]))
                        continue;
                    // a corner that another vertex sits inside is no ear either, which the containment refuses
                    bool blocked = false;
                    for (const std::size_t other : remaining) {
                        // the three corner points are the ear itself, which no containment test applies to
                        if (other == previous || other == point || other == next)
                            continue;
                        // a vertex inside the candidate ear blocks it, which the scan records
                        if (inside_triangle(polygon[other], polygon[previous], polygon[point], polygon[next]))
                            blocked = true;
                    }
                    // a blocked corner is the next candidate, which the walk skips
                    if (blocked)
                        continue;
                    // the ear joins the triangle list, and its corner leaves the ring
                    triangles.push_back({previous, point, next});
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(at));
                    clipped = true;
                    break;
                }
                // a ring with no ear falls back to a fan, which a self intersecting contour can produce
                if (!clipped) {
                    for (std::size_t index = 1; index + 1 < remaining.size(); index++)
                        triangles.push_back({remaining[0], remaining[index], remaining[index + 1]});
                    return triangles;
                }
            }
            // the last three points are the closing triangle, which the loop leaves
            triangles.push_back({remaining[0], remaining[1], remaining[2]});
            return triangles;
        }

        // the inward normal of a unit direction, which the winding normalization fixes to the left side
        Point inward_normal(float dx, float dy) { return Point{.x = dy, .y = -dx}; }

        // the two points the bevel choice picks on one side of a join, which the miter collapses to one (nanovg technique)
        void choose_bevel(unsigned flags, const ContourPoint& previous, const ContourPoint& point, float offset, Point& start, Point& end) {
            // a bevelled join picks the two edge offsets, which a mitered one collapses onto the averaged normal
            if ((flags & InnerBevel) != 0u) {
                // the two edge offsets of the incoming and the outgoing segment
                const Point normal0 = inward_normal(previous.dx, previous.dy);
                const Point normal1 = inward_normal(point.dx, point.dy);
                start = Point{.x = point.position.x + normal0.x * offset, .y = point.position.y + normal0.y * offset};
                end = Point{.x = point.position.x + normal1.x * offset, .y = point.position.y + normal1.y * offset};
                return;
            }
            // the mitered join collapses both offsets onto the averaged normal
            const Point miter = Point{.x = point.position.x + point.miter_x * offset, .y = point.position.y + point.miter_y * offset};
            start = miter;
            end = miter;
        }

        // append the bevel or miter join geometry a point carries, which the strip stitches through (nanovg technique)
        void emit_bevel_join(FrameBatch& sink, const ContourPoint& previous, const ContourPoint& point, float width, float lateral_in, float lateral_out) {
            // the inward normals of the two segments
            const Point normal0 = inward_normal(previous.dx, previous.dy);
            const Point normal1 = inward_normal(point.dx, point.dy);
            // the sequence the join appends, which mirrors between the two turn sides
            std::vector<StripVertex> sequence;
            // the left turning side picks the inner offsets, which the mirrored one picks on the outer side
            if ((point.flags & Left) != 0u) {
                // the inner side points the bevel choice picks
                Point inner0 = Point{};
                Point inner1 = Point{};
                choose_bevel(point.flags, previous, point, width, inner0, inner1);
                // the cross from the inner side to the outer side of the incoming segment
                sequence.push_back(StripVertex{.x = inner0.x, .y = inner0.y, .lateral = lateral_in});
                sequence.push_back(StripVertex{.x = point.position.x - normal0.x * width, .y = point.position.y - normal0.y * width, .lateral = lateral_out});
                // a bevelled corner splits the outer edge, which a mitered one stitches through the centre
                if ((point.flags & Bevel) != 0u) {
                    sequence.push_back(StripVertex{.x = inner0.x, .y = inner0.y, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = point.position.x - normal0.x * width, .y = point.position.y - normal0.y * width, .lateral = lateral_out});
                    sequence.push_back(StripVertex{.x = inner1.x, .y = inner1.y, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = point.position.x - normal1.x * width, .y = point.position.y - normal1.y * width, .lateral = lateral_out});
                }
                else {
                    // the mitered outer point the wedge stitches through
                    const Point miter = Point{.x = point.position.x - point.miter_x * width, .y = point.position.y - point.miter_y * width};
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                    sequence.push_back(StripVertex{.x = point.position.x - normal0.x * width, .y = point.position.y - normal0.y * width, .lateral = lateral_out});
                    sequence.push_back(StripVertex{.x = miter.x, .y = miter.y, .lateral = lateral_out});
                    sequence.push_back(StripVertex{.x = miter.x, .y = miter.y, .lateral = lateral_out});
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                    sequence.push_back(StripVertex{.x = point.position.x - normal1.x * width, .y = point.position.y - normal1.y * width, .lateral = lateral_out});
                }
                // the cross to the outer side of the outgoing segment
                sequence.push_back(StripVertex{.x = inner1.x, .y = inner1.y, .lateral = lateral_in});
                sequence.push_back(StripVertex{.x = point.position.x - normal1.x * width, .y = point.position.y - normal1.y * width, .lateral = lateral_out});
            }
            else {
                // the outer side points the bevel choice picks, which the mirrored branch offsets negatively
                Point outer0 = Point{};
                Point outer1 = Point{};
                choose_bevel(point.flags, previous, point, -width, outer0, outer1);
                // the cross from the inner side to the outer side of the incoming segment
                sequence.push_back(StripVertex{.x = point.position.x + normal0.x * width, .y = point.position.y + normal0.y * width, .lateral = lateral_in});
                sequence.push_back(StripVertex{.x = outer0.x, .y = outer0.y, .lateral = lateral_out});
                // a bevelled corner splits the outer edge, which a mitered one stitches through the centre
                if ((point.flags & Bevel) != 0u) {
                    sequence.push_back(StripVertex{.x = point.position.x + normal0.x * width, .y = point.position.y + normal0.y * width, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = outer0.x, .y = outer0.y, .lateral = lateral_out});
                    sequence.push_back(StripVertex{.x = point.position.x + normal1.x * width, .y = point.position.y + normal1.y * width, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = outer1.x, .y = outer1.y, .lateral = lateral_out});
                }
                else {
                    // the mitered inner point the wedge stitches through
                    const Point miter = Point{.x = point.position.x + point.miter_x * width, .y = point.position.y + point.miter_y * width};
                    sequence.push_back(StripVertex{.x = point.position.x + normal0.x * width, .y = point.position.y + normal0.y * width, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                    sequence.push_back(StripVertex{.x = miter.x, .y = miter.y, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = miter.x, .y = miter.y, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = point.position.x + normal1.x * width, .y = point.position.y + normal1.y * width, .lateral = lateral_in});
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                }
                // the cross to the outer side of the outgoing segment
                sequence.push_back(StripVertex{.x = point.position.x + normal1.x * width, .y = point.position.y + normal1.y * width, .lateral = lateral_in});
                sequence.push_back(StripVertex{.x = outer1.x, .y = outer1.y, .lateral = lateral_out});
            }
            append_vertices(sink, sequence);
        }

        // append the round join geometry a point carries, which arcs the widening side (nanovg technique)
        void emit_round_join(FrameBatch& sink, const ContourPoint& previous, const ContourPoint& point, float width, int segments) {
            // the inward normals of the two segments
            const Point normal0 = inward_normal(previous.dx, previous.dy);
            const Point normal1 = inward_normal(point.dx, point.dy);
            // the sequence the join appends
            std::vector<StripVertex> sequence;
            // the left turning side arcs the outer edge, which the mirrored one arcs on the inner side
            const bool left = (point.flags & Left) != 0u;
            // the arc sits on the outward normal side for a left turn and on the inward one for a right turn
            const float side = left ? -1.0f : 1.0f;
            // the angle the arc starts from, which the arc side normal carries
            const float start = std::atan2(side * normal0.y, side * normal0.x);
            // the sweep is the turn itself, which the two directions enclose and the turning side orients
            const float dot = std::clamp(previous.dx * point.dx + previous.dy * point.dy, -1.0f, 1.0f);
            const float sweep = (left ? -1.0f : 1.0f) * std::acos(dot);
            // the cross from the arc side of the incoming segment to the other side, which the bevel choice picks
            if (left) {
                // the inner side points the bevel choice picks
                Point inner0 = Point{};
                Point inner1 = Point{};
                choose_bevel(point.flags, previous, point, width, inner0, inner1);
                sequence.push_back(StripVertex{.x = inner0.x, .y = inner0.y, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = point.position.x - normal0.x * width, .y = point.position.y - normal0.y * width, .lateral = 1.0f});
            }
            else {
                // the outer side points the bevel choice picks, which the mirrored branch offsets negatively
                Point outer0 = Point{};
                Point outer1 = Point{};
                choose_bevel(point.flags, previous, point, -width, outer0, outer1);
                sequence.push_back(StripVertex{.x = point.position.x + normal0.x * width, .y = point.position.y + normal0.y * width, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = outer0.x, .y = outer0.y, .lateral = 1.0f});
            }
            // the arc steps the clamped segment count asks for
            const int steps = std::clamp(static_cast<int>(std::ceil(std::abs(sweep) / PI * static_cast<float>(segments))), 2, segments);
            for (int index = 0; index < steps; index++) {
                // the arc position interpolates from the start angle by the sweep
                const float t = static_cast<float>(index) / static_cast<float>(steps - 1);
                const float angle = start + t * sweep;
                // the centre vertex holds the coverage peak while the arc vertex carries the fringe
                if (left) {
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                    sequence.push_back(StripVertex{.x = point.position.x + std::cos(angle) * width, .y = point.position.y + std::sin(angle) * width, .lateral = 1.0f});
                }
                else {
                    sequence.push_back(StripVertex{.x = point.position.x + std::cos(angle) * width, .y = point.position.y + std::sin(angle) * width, .lateral = 0.0f});
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                }
            }
            // the cross to the outgoing segment, which mirrors the incoming cross
            if (left) {
                // the inner side points the bevel choice picks
                Point inner0 = Point{};
                Point inner1 = Point{};
                choose_bevel(point.flags, previous, point, width, inner0, inner1);
                sequence.push_back(StripVertex{.x = inner1.x, .y = inner1.y, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = point.position.x - normal1.x * width, .y = point.position.y - normal1.y * width, .lateral = 1.0f});
            }
            else {
                // the outer side points the bevel choice picks, which the mirrored branch offsets negatively
                Point outer0 = Point{};
                Point outer1 = Point{};
                choose_bevel(point.flags, previous, point, -width, outer0, outer1);
                sequence.push_back(StripVertex{.x = point.position.x + normal1.x * width, .y = point.position.y + normal1.y * width, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = outer1.x, .y = outer1.y, .lateral = 1.0f});
            }
            append_vertices(sink, sequence);
        }

        // append the start cap of an open contour, which the direction leaves from (nanovg technique)
        void emit_cap_start(FrameBatch& sink, const ContourPoint& point, float dx, float dy, float width, Cap cap, int segments) {
            // the inward normal of the direction, which the cap edges offset along
            const Point normal = inward_normal(dx, dy);
            // the sequence the cap appends
            std::vector<StripVertex> sequence;
            // the round cap fans a half disc of segments from the end point
            if (cap == Cap::Round) {
                // the arc steps from one side of the end to the other through the apex
                for (int index = 0; index < segments; index++) {
                    // the arc angle interpolates over the half disc
                    const float angle = static_cast<float>(index) / static_cast<float>(segments - 1) * PI;
                    // the arc position rotates the half width around the end
                    const float ax = std::cos(angle) * width;
                    const float ay = std::sin(angle) * width;
                    // the rim vertex carries the fringe while the centre carries the peak
                    sequence.push_back(StripVertex{.x = point.position.x - normal.x * ax - dx * ay, .y = point.position.y - normal.y * ax - dy * ay, .lateral = 0.0f});
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                }
                // the closing pair lands on the two sides of the end point
                sequence.push_back(StripVertex{.x = point.position.x + normal.x * width, .y = point.position.y + normal.y * width, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = point.position.x - normal.x * width, .y = point.position.y - normal.y * width, .lateral = 1.0f});
                append_vertices(sink, sequence);
                return;
            }
            // the butt cap ramps across half a fringe at the end, which the square cap pushes out by the half width
            const Point soft = cap == Cap::Square ? Point{.x = point.position.x - dx * width, .y = point.position.y - dy * width} : Point{.x = point.position.x - dx * FRINGE * 0.5f, .y = point.position.y - dy * FRINGE * 0.5f};
            // the hard edge sits a fringe inside the soft one, which the stroke body continues from
            const Point hard = cap == Cap::Square ? Point{.x = point.position.x - dx * (width - FRINGE), .y = point.position.y - dy * (width - FRINGE)} : Point{.x = point.position.x + dx * FRINGE * 0.5f, .y = point.position.y + dy * FRINGE * 0.5f};
            // the strip crosses from the soft edge to the hard edge on both sides
            sequence.push_back(StripVertex{.x = soft.x + normal.x * width, .y = soft.y + normal.y * width, .lateral = 0.0f, .longitudinal = 0.0f});
            sequence.push_back(StripVertex{.x = soft.x - normal.x * width, .y = soft.y - normal.y * width, .lateral = 1.0f, .longitudinal = 0.0f});
            sequence.push_back(StripVertex{.x = hard.x + normal.x * width, .y = hard.y + normal.y * width, .lateral = 0.0f, .longitudinal = 1.0f});
            sequence.push_back(StripVertex{.x = hard.x - normal.x * width, .y = hard.y - normal.y * width, .lateral = 1.0f, .longitudinal = 1.0f});
            append_vertices(sink, sequence);
        }

        // append the end cap of an open contour, which the direction arrives at (nanovg technique)
        void emit_cap_end(FrameBatch& sink, const ContourPoint& point, float dx, float dy, float width, Cap cap, int segments) {
            // the inward normal of the direction, which the cap edges offset along
            const Point normal = inward_normal(dx, dy);
            // the sequence the cap appends
            std::vector<StripVertex> sequence;
            // the round cap fans a half disc of segments from the end point
            if (cap == Cap::Round) {
                // the opening pair lands on the two sides of the end point
                sequence.push_back(StripVertex{.x = point.position.x + normal.x * width, .y = point.position.y + normal.y * width, .lateral = 0.0f});
                sequence.push_back(StripVertex{.x = point.position.x - normal.x * width, .y = point.position.y - normal.y * width, .lateral = 1.0f});
                // the arc steps from one side of the end to the other through the apex
                for (int index = 0; index < segments; index++) {
                    // the arc angle interpolates over the half disc
                    const float angle = static_cast<float>(index) / static_cast<float>(segments - 1) * PI;
                    // the arc position rotates the half width around the end
                    const float ax = std::cos(angle) * width;
                    const float ay = std::sin(angle) * width;
                    // the centre vertex carries the peak while the rim carries the fringe
                    sequence.push_back(StripVertex{.x = point.position.x, .y = point.position.y, .lateral = 0.5f});
                    sequence.push_back(StripVertex{.x = point.position.x - normal.x * ax + dx * ay, .y = point.position.y - normal.y * ax + dy * ay, .lateral = 0.0f});
                }
                append_vertices(sink, sequence);
                return;
            }
            // the butt cap ramps across half a fringe at the end, which the square cap pushes out by the half width
            const Point hard = cap == Cap::Square ? Point{.x = point.position.x + dx * (width - FRINGE), .y = point.position.y + dy * (width - FRINGE)} : Point{.x = point.position.x - dx * FRINGE * 0.5f, .y = point.position.y - dy * FRINGE * 0.5f};
            // the soft edge sits a fringe past the hard one, which the fringe fades out through
            const Point soft = cap == Cap::Square ? Point{.x = point.position.x + dx * width, .y = point.position.y + dy * width} : Point{.x = point.position.x + dx * FRINGE * 0.5f, .y = point.position.y + dy * FRINGE * 0.5f};
            // the strip crosses from the hard edge to the soft edge on both sides
            sequence.push_back(StripVertex{.x = hard.x + normal.x * width, .y = hard.y + normal.y * width, .lateral = 0.0f, .longitudinal = 1.0f});
            sequence.push_back(StripVertex{.x = hard.x - normal.x * width, .y = hard.y - normal.y * width, .lateral = 1.0f, .longitudinal = 1.0f});
            sequence.push_back(StripVertex{.x = soft.x + normal.x * width, .y = soft.y + normal.y * width, .lateral = 0.0f, .longitudinal = 0.0f});
            sequence.push_back(StripVertex{.x = soft.x - normal.x * width, .y = soft.y - normal.y * width, .lateral = 1.0f, .longitudinal = 0.0f});
            append_vertices(sink, sequence);
        }
    } // namespace

    Transform invert_transform(const Transform& transform) {
        // the determinant of the two by two linear part decides invertibility
        const float determinant = transform.scale_x * transform.scale_y - transform.skew_x * transform.skew_y;
        // a singular transform inverts to the identity, which a degenerate draw never maps through
        if (std::abs(determinant) < 0.000001f)
            return Transform{};
        // the inverse of the linear part is the adjugate over the determinant
        const float inverse_a = transform.scale_y / determinant;
        const float inverse_b = -transform.skew_x / determinant;
        const float inverse_c = -transform.skew_y / determinant;
        const float inverse_d = transform.scale_x / determinant;
        // the translation maps through the inverse linear part
        const float inverse_tx = -(inverse_a * transform.translate_x + inverse_b * transform.translate_y);
        const float inverse_ty = -(inverse_c * transform.translate_x + inverse_d * transform.translate_y);
        // the albor transform packs the matrix row wise
        return Transform{.scale_x = inverse_a, .skew_x = inverse_b, .translate_x = inverse_tx, .skew_y = inverse_c, .scale_y = inverse_d, .translate_y = inverse_ty};
    }

    int curve_divs(float radius, float arc, float tess_tolerance) {
        // a radius and a tolerance that no arc can divide refuse the two segment floor
        if (radius <= 0.0f || tess_tolerance <= 0.0f)
            return 2;
        // the angle one segment spans shrinks as the radius grows, which keeps the chord error at the tolerance
        const float step = std::acos(radius / (radius + tess_tolerance)) * 2.0f;
        // an arc needs at least two segments, which a half circle of any radius carries
        return std::max(2, static_cast<int>(std::ceil(arc / step)));
    }

    Contour rect_contour(const Rect& rect) {
        // the contour walks the rectangle down the left edge, across the bottom, up the right and back the top
        Contour contour;
        // the four corners in winding order
        const Point corners[4] = {
            Point{.x = rect.x, .y = rect.y},
            Point{.x = rect.x, .y = rect.y + rect.height},
            Point{.x = rect.x + rect.width, .y = rect.y + rect.height},
            Point{.x = rect.x + rect.width, .y = rect.y},
        };
        // every corner joins the contour, which the close stamps as closed
        for (const Point& corner : corners) {
            // the corner point carries the corner flag the join calculation reads
            ContourPoint point;
            point.position = corner;
            point.flags = ContourPointFlags::Corner;
            contour.points.push_back(point);
        }
        contour.closed = true;
        stamp_directions(contour);
        return contour;
    }

    Contour rounded_rect_contour(const Rect& rect, float radius, float tess_tolerance) {
        // the radius clamps into the shorter extent, which a larger radius cannot exceed
        const float clamped = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
        // a radiusless rounded rectangle is the plain rectangle, which shares its winding and its corners
        if (clamped <= 0.0f)
            return rect_contour(rect);
        // the contour walks the rounded rectangle edge by edge with one arc per corner
        Contour contour;
        // each corner arc needs the segment count the tolerance asks for
        const int segments = curve_divs(clamped, PI * 0.5f, tess_tolerance);
        // the corner arc centres, which walk the contour across the bottom, up the right and back the top
        const Point centers[4] = {
            Point{.x = rect.x + clamped, .y = rect.y + rect.height - clamped},
            Point{.x = rect.x + rect.width - clamped, .y = rect.y + rect.height - clamped},
            Point{.x = rect.x + rect.width - clamped, .y = rect.y + clamped},
            Point{.x = rect.x + clamped, .y = rect.y + clamped},
        };
        // the angle each corner arc starts from, which the straight edges connect between
        const float starts[4] = {PI, PI * 0.5f, 0.0f, -PI * 0.5f};
        // the sweep runs backwards a quarter circle per corner, which keeps the contour counter clockwise on screen
        const float sweep = -PI * 0.5f;
        // every corner arc appends its points, which the straight edges the closure implies connect
        for (std::size_t corner = 0; corner < 4; corner++) {
            // the arc steps from its start angle by the sweep
            for (int step = 0; step <= segments; step++) {
                // the angle interpolates along the arc
                const float angle = starts[corner] + sweep * static_cast<float>(step) / static_cast<float>(segments);
                // the arc point sits at the centre plus the rotated radius, which the join calculation corners at
                ContourPoint point;
                point.position = Point{.x = centers[corner].x + std::cos(angle) * clamped, .y = centers[corner].y + std::sin(angle) * clamped};
                point.flags = Corner;
                contour.points.push_back(point);
            }
        }
        contour.closed = true;
        stamp_directions(contour);
        return contour;
    }

    EmittedRange expand_fill(FrameBatch& sink, Contour& contour) {
        // the fill fringe joins with the miter the technique fixes
        calculate_joins(contour, FRINGE, Join::Miter, FILL_MITER_LIMIT);
        // a contour without a ring draws nothing
        if (contour.points.size() < 3)
            return EmittedRange{};
        // the range the expansion appends starts at the current index count
        const std::uint32_t index_begin = static_cast<std::uint32_t>(sink.indices.size());
        // the inset offsets the ring half a fringe inward, which the fringe strip straddles the boundary with
        const float offset = FRINGE * 0.5f;
        // the ring the fill covers, which a right turned bevel splits into the two edge insets
        std::vector<std::uint32_t> ring;
        // the ring positions, which a concave contour triangulates instead of fanning
        std::vector<Point> ring_points;
        const ContourPoint* previous = &contour.points.back();
        for (const ContourPoint& point : contour.points) {
            // a bevelled right corner splits the ring, which every other corner collapses onto the averaged normal
            if ((point.flags & Bevel) != 0u && (point.flags & Left) == 0u) {
                // the two inward edge offsets of the split corner
                const Point normal0 = inward_normal(previous->dx, previous->dy);
                const Point normal1 = inward_normal(point.dx, point.dy);
                ring.push_back(emit_vertex(sink, point.position.x + normal0.x * offset, point.position.y + normal0.y * offset, 1.0f, 1.0f));
                ring_points.push_back(Point{.x = point.position.x + normal0.x * offset, .y = point.position.y + normal0.y * offset});
                ring.push_back(emit_vertex(sink, point.position.x + normal1.x * offset, point.position.y + normal1.y * offset, 1.0f, 1.0f));
                ring_points.push_back(Point{.x = point.position.x + normal1.x * offset, .y = point.position.y + normal1.y * offset});
            }
            else {
                // the single inset point the averaged normal carries
                ring.push_back(emit_vertex(sink, point.position.x + point.miter_x * offset, point.position.y + point.miter_y * offset, 1.0f, 1.0f));
                ring_points.push_back(Point{.x = point.position.x + point.miter_x * offset, .y = point.position.y + point.miter_y * offset});
            }
            // the previous point advances with the walk
            previous = &point;
        }
        // a convex ring fans, which covers it exactly
        if (contour.convex) {
            for (std::size_t index = 1; index + 1 < ring.size(); index++)
                emit_triangle(sink, ring[0], ring[index], ring[index + 1]);
        }
        else {
            // a concave ring triangulates, because a fan would paint the notches it spans
            for (const std::array<std::size_t, 3>& triangle : ear_clip(ring_points))
                emit_triangle(sink, ring[triangle[0]], ring[triangle[1]], ring[triangle[2]]);
        }
        // the fringe strip runs beside the ring, which the join sequences split on either side
        const std::uint32_t strip_first = static_cast<std::uint32_t>(sink.vertices.size());
        previous = &contour.points.back();
        for (const ContourPoint& point : contour.points) {
            // a join that bevels on either side emits the join geometry, which a plain corner crosses directly
            if ((point.flags & (Bevel | InnerBevel)) != 0u) {
                emit_bevel_join(sink, *previous, point, offset, 1.0f, 0.0f);
            }
            else {
                // the plain corner crosses from the inset ring to the outward fringe
                emit_vertex(sink, point.position.x + point.miter_x * offset, point.position.y + point.miter_y * offset, 1.0f, 1.0f);
                emit_vertex(sink, point.position.x - point.miter_x * offset, point.position.y - point.miter_y * offset, 0.0f, 1.0f);
            }
            // the previous point advances with the walk
            previous = &point;
        }
        // the wrap repeats the first pair, which closes the strip through the seam
        emit_vertex(sink, sink.vertices[strip_first].x, sink.vertices[strip_first].y, sink.vertices[strip_first].lateral, 1.0f);
        emit_vertex(sink, sink.vertices[strip_first + 1].x, sink.vertices[strip_first + 1].y, sink.vertices[strip_first + 1].lateral, 1.0f);
        // the strip runs from the first corner through the wrap
        emit_strip(sink, strip_first, sink.vertices.size() - strip_first);
        return EmittedRange{.index_begin = index_begin, .index_count = static_cast<std::uint32_t>(sink.indices.size()) - index_begin};
    }

    StrokeEmission expand_stroke(FrameBatch& sink, Contour& contour, float half_width, Cap cap, Join join, float miter_limit, float tess_tolerance) {
        // a contour with no width and no segment carries no stroke at all
        if (contour.points.size() < 2 || half_width <= 0.0f)
            return StrokeEmission{.alpha = 0.0f};
        // the range the expansion appends starts at the current index count
        const std::uint32_t index_begin = static_cast<std::uint32_t>(sink.indices.size());
        // a stroke thinner than the fringe emulates its coverage with the linear ratio, which preserves its area
        float alpha = 1.0f;
        if (half_width < FRINGE * 0.5f) {
            // the ratio of the width to the fringe the emulation clamps at
            alpha = std::clamp(half_width / (FRINGE * 0.5f), 0.0f, 1.0f);
            half_width = FRINGE * 0.5f;
        }
        // the outline sits half a fringe past the half width, which the folded ramp straddles
        const float width = half_width + FRINGE * 0.5f;
        // the joins measure against the expanded width, which the caps and the fringe share
        calculate_joins(contour, width, join, miter_limit);
        // the arc segments a half circle of the expanded width divides into
        const int segments = curve_divs(width, PI, tess_tolerance);
        // a closed contour strips through the wrap, which an open one caps at both ends
        if (contour.closed) {
            // the strip starts at the first station and closes through the wrap
            const std::uint32_t first = static_cast<std::uint32_t>(sink.vertices.size());
            // the stations walk the whole contour, which the join geometry fills at the corners
            const ContourPoint* previous = &contour.points.back();
            for (const ContourPoint& point : contour.points) {
                // a join that bevels on either side emits the join geometry, which a plain one crosses directly
                if ((point.flags & (Bevel | InnerBevel)) != 0u) {
                    // a round join arcs, which every other join bevels or miters
                    if (join == Join::Round && (point.flags & Bevel) != 0u)
                        emit_round_join(sink, *previous, point, width, segments);
                    else
                        emit_bevel_join(sink, *previous, point, width, 0.0f, 1.0f);
                }
                else {
                    // the plain station crosses the band along the averaged normal
                    emit_vertex(sink, point.position.x + point.miter_x * width, point.position.y + point.miter_y * width, 0.0f, 1.0f);
                    emit_vertex(sink, point.position.x - point.miter_x * width, point.position.y - point.miter_y * width, 1.0f, 1.0f);
                }
                // the previous point advances with the walk
                previous = &point;
            }
            // the wrap repeats the first pair, which closes the strip through the seam
            emit_vertex(sink, sink.vertices[first].x, sink.vertices[first].y, sink.vertices[first].lateral, 1.0f);
            emit_vertex(sink, sink.vertices[first + 1].x, sink.vertices[first + 1].y, sink.vertices[first + 1].lateral, 1.0f);
            // the strip runs from the first station through the wrap
            emit_strip(sink, first, sink.vertices.size() - first);
            return StrokeEmission{.range = EmittedRange{.index_begin = index_begin, .index_count = static_cast<std::uint32_t>(sink.indices.size()) - index_begin}, .alpha = alpha, .ramp = width / FRINGE};
        }
        // an open contour caps at both ends, which the walk sequences between
        const ContourPoint& start = contour.points.front();
        const ContourPoint& after_start = contour.points[1];
        const ContourPoint& before_end = contour.points[contour.points.size() - 2];
        const ContourPoint& end = contour.points.back();
        // the unit directions of the first and the last segment
        float start_dx = after_start.position.x - start.position.x;
        float start_dy = after_start.position.y - start.position.y;
        const float start_length = std::sqrt(start_dx * start_dx + start_dy * start_dy);
        if (start_length > 0.0f) {
            start_dx /= start_length;
            start_dy /= start_length;
        }
        float end_dx = end.position.x - before_end.position.x;
        float end_dy = end.position.y - before_end.position.y;
        const float end_length = std::sqrt(end_dx * end_dx + end_dy * end_dy);
        if (end_length > 0.0f) {
            end_dx /= end_length;
            end_dy /= end_length;
        }
        // the strip starts with the start cap
        const std::uint32_t first = static_cast<std::uint32_t>(sink.vertices.size());
        emit_cap_start(sink, start, start_dx, start_dy, width, cap, segments);
        // the body stations run between the caps, which the join geometry fills
        for (std::size_t index = 1; index + 1 < contour.points.size(); index++) {
            // the station point and its previous point
            const ContourPoint& point = contour.points[index];
            const ContourPoint& previous = contour.points[index - 1];
            // a join that bevels on either side emits the join geometry, which a plain one crosses directly
            if ((point.flags & (Bevel | InnerBevel)) != 0u) {
                // a round join arcs, which every other join bevels or miters
                if (join == Join::Round && (point.flags & Bevel) != 0u)
                    emit_round_join(sink, previous, point, width, segments);
                else
                    emit_bevel_join(sink, previous, point, width, 0.0f, 1.0f);
            }
            else {
                // the plain station crosses the band along the averaged normal
                emit_vertex(sink, point.position.x + point.miter_x * width, point.position.y + point.miter_y * width, 0.0f, 1.0f);
                emit_vertex(sink, point.position.x - point.miter_x * width, point.position.y - point.miter_y * width, 1.0f, 1.0f);
            }
        }
        // the strip ends with the end cap
        emit_cap_end(sink, end, end_dx, end_dy, width, cap, segments);
        // the strip runs from the start cap through the end cap
        emit_strip(sink, first, sink.vertices.size() - first);
        return StrokeEmission{.range = EmittedRange{.index_begin = index_begin, .index_count = static_cast<std::uint32_t>(sink.indices.size()) - index_begin}, .alpha = alpha, .ramp = width / FRINGE};
    }
} // namespace albor::render
