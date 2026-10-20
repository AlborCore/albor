#include "render/render-albor/albor_fill.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "render/render-albor/albor_geometry.h"

namespace albor::render
{
    namespace
    {
        // the tolerance two rows or two crossings compare equal at, which a flattened arc carries
        constexpr float SWEEP_TOLERANCE = 0.0001f;

        // the depth a slab splits at before its crossings are filled as they stand, which a fan of meetings cannot exhaust
        constexpr int SLAB_SPLIT_DEPTH = 8;

        // one edge the sweep meets a slab with, carrying its x at the two rows and the winding it adds
        struct SweepCrossing
        {
            // the x where the edge meets the top row
            float top = 0.0f;

            // the x where the edge meets the bottom row
            float bottom = 0.0f;

            // the winding the edge contributes, which is negative for a ring the normalization reversed
            int winding = 0;
        };

        // append one covered vertex and hand back its index
        std::uint32_t emit_vertex(FrameBatch& sink, float x, float y, float lateral) {
            // the vertex joins the frame mesh with the full longitudinal coverage a fill carries
            BatchVertex vertex;
            vertex.x = x;
            vertex.y = y;
            vertex.lateral = lateral;
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

        // the coverage one ring contributes at a point, which either fill rule reads (§6.2)
        struct CoverageCount
        {
            // the signed winding the ring adds, which a non zero fill reads
            int winding = 0;

            // the number of edges the ray crosses, which an even odd fill reads
            int crossings = 0;
        };

        // count the crossings and the winding one ring contributes at a point (§6.2)
        CoverageCount coverage_along(const FillRing& ring, Point point) {
            // the direction the normalization reversed, which inverts the winding every crossing contributes
            const int direction = ring.reversed ? -1 : 1;
            // the crossings the point row meets, which add up to the winding
            CoverageCount coverage;
            for (std::size_t index = 0; index < ring.points.size(); index++) {
                // the edge runs from this point to the next, which wraps on the ring
                const Point& from = ring.points[index];
                const Point& to = ring.points[(index + 1) % ring.points.size()];
                // a horizontal edge crosses no ray, which the count skips
                if (from.y == to.y)
                    continue;
                // only an edge that carries the point row on one side meets the ray
                if ((from.y > point.y) == (to.y > point.y))
                    continue;
                // the x where the edge meets the point row decides which side the point sits on
                const float crossing = from.x + (point.y - from.y) / (to.y - from.y) * (to.x - from.x);
                // an edge the ray passes stands on the point's right, which the count reads
                if (point.x < crossing) {
                    coverage.winding += (to.y > from.y ? 1 : -1) * direction;
                    coverage.crossings++;
                }
            }
            return coverage;
        }

        // report whether a point is covered, which the fill rule decides across every ring (§6.2)
        bool inside_at(std::span<const FillRing> rings, Point point, FillRule rule) {
            // an even odd fill counts crossings regardless of their direction, so a same winding overlap cuts
            if (rule == FillRule::EvenOdd) {
                int crossings = 0;
                for (const FillRing& ring : rings)
                    crossings += coverage_along(ring, point).crossings;
                return crossings % 2 != 0;
            }
            // a non zero fill adds the windings, so a hole is a ring that winds the other way
            int winding = 0;
            for (const FillRing& ring : rings)
                winding += coverage_along(ring, point).winding;
            return winding != 0;
        }

        // collect the rows every ring turns at, which bound the slabs the sweep walks
        std::vector<float> sweep_rows(std::span<const FillRing> rings) {
            // every ring vertex contributes its row, which the sort then orders
            std::vector<float> rows;
            for (const FillRing& ring : rings) {
                for (const Point& point : ring.points)
                    rows.push_back(point.y);
            }
            std::sort(rows.begin(), rows.end());
            // a repeated row bounds no slab, so it collapses onto the first one
            rows.erase(std::unique(rows.begin(), rows.end(), [](float lhs, float rhs) { return std::abs(lhs - rhs) <= SWEEP_TOLERANCE; }), rows.end());
            return rows;
        }

        // collect the crossings every edge that spans the slab makes at its two rows
        std::vector<SweepCrossing> slab_crossings(std::span<const FillRing> rings, float top, float bottom) {
            // the crossings the slab collects, which the caller orders by x
            std::vector<SweepCrossing> crossings;
            for (const FillRing& ring : rings) {
                for (std::size_t index = 0; index < ring.points.size(); index++) {
                    // the edge runs from this point to the next, which wraps on the ring
                    const Point& from = ring.points[index];
                    const Point& to = ring.points[(index + 1) % ring.points.size()];
                    // a horizontal edge lies on a row and spans no slab
                    if (from.y == to.y)
                        continue;
                    // an edge that ends inside the slab spans only part of it, which no crossing covers
                    if (std::min(from.y, to.y) > top + SWEEP_TOLERANCE || std::max(from.y, to.y) < bottom - SWEEP_TOLERANCE)
                        continue;
                    // the two crossings sit where the edge meets each row, which a sloped edge splits apart
                    const float slope = (to.x - from.x) / (to.y - from.y);
                    crossings.push_back(SweepCrossing{
                        .top = from.x + (top - from.y) * slope,
                        .bottom = from.x + (bottom - from.y) * slope,
                        .winding = (to.y > from.y ? 1 : -1) * (ring.reversed ? -1 : 1),
                    });
                }
            }
            return crossings;
        }

        // the row inside the slab where two crossings swap places, or the bottom when none does
        float crossing_row(const std::vector<SweepCrossing>& crossings, float top, float bottom) {
            // the height the slab spans, which an edge slope divides by
            const float height = bottom - top;
            // the first row where two edges meet, which the slab has to split at
            float split = bottom;
            for (std::size_t first = 0; first < crossings.size(); first++) {
                for (std::size_t second = first + 1; second < crossings.size(); second++) {
                    // the two edges in x per row, which a straight edge holds across the slab
                    const float first_slope = (crossings[first].bottom - crossings[first].top) / height;
                    const float second_slope = (crossings[second].bottom - crossings[second].top) / height;
                    // parallel edges never meet, which the difference of their slopes tells
                    const float slope = first_slope - second_slope;
                    if (std::abs(slope) <= SWEEP_TOLERANCE)
                        continue;
                    // the row where the two edges meet, which is a fraction of the slab
                    const float row = top + (crossings[second].top - crossings[first].top) / slope;
                    // a meeting outside the slab is no split, and one already found sits above
                    if (row <= top + SWEEP_TOLERANCE || row >= split - SWEEP_TOLERANCE)
                        continue;
                    split = row;
                }
            }
            return split;
        }

        // append the trapezoids one slab covers, splitting it at every row where its edges meet
        void sweep_slab(std::span<const FillRing> rings, float top, float bottom, FillRule rule, int depth, std::vector<FillTrapezoid>& out) {
            // the crossings this slab sees, which the split test and the coverage walk both read
            const std::vector<SweepCrossing> crossings = slab_crossings(rings, top, bottom);
            // two edges that meet inside the slab swap sides, so the slab splits there rather than spanning both
            const float split = crossing_row(crossings, top, bottom);
            if (split < bottom - SWEEP_TOLERANCE && depth < SLAB_SPLIT_DEPTH) {
                sweep_slab(rings, top, split, rule, depth + 1, out);
                sweep_slab(rings, split, bottom, rule, depth + 1, out);
                return;
            }
            // the crossings order left to right, which the coverage walk reads
            std::vector<SweepCrossing> ordered = crossings;
            std::sort(ordered.begin(), ordered.end(), [](const SweepCrossing& lhs, const SweepCrossing& rhs) { return lhs.top + lhs.bottom < rhs.top + rhs.bottom; });
            // the winding the walk accumulates, which covers the span while it is non zero
            int winding = 0;
            // the parity the walk toggles, which covers the span while it is odd
            bool parity = false;
            for (std::size_t index = 0; index < ordered.size(); index++) {
                // the edge on the left opens or closes the span it bounds
                winding += ordered[index].winding;
                parity = !parity;
                // a span the rule puts back outside is a hole, which stays open
                const bool covered = rule == FillRule::EvenOdd ? parity : winding != 0;
                if (!covered || index + 1 >= ordered.size())
                    continue;
                // the two crossings that bound the covered span
                const SweepCrossing& left = ordered[index];
                const SweepCrossing& right = ordered[index + 1];
                // a pair with no width covers nothing, which two rings sharing an edge produce
                if (std::abs(right.top - left.top) <= SWEEP_TOLERANCE && std::abs(right.bottom - left.bottom) <= SWEEP_TOLERANCE)
                    continue;
                // the trapezoid joins the sweep, which the caller turns into two triangles
                out.push_back(FillTrapezoid{
                    .top = top,
                    .bottom = bottom,
                    .left_top = left.top,
                    .left_bottom = left.bottom,
                    .right_top = right.top,
                    .right_bottom = right.bottom,
                });
            }
        }

        // the miter normal of a point, clamped to the miter limit so a needle corner cannot spike the ring (§6.2)
        Point clamped_miter(const ContourPoint& point) {
            // the averaged normal is unit length for every ordinary corner, which the limit leaves alone
            const float magnitude = std::sqrt(point.miter_x * point.miter_x + point.miter_y * point.miter_y);
            if (magnitude <= FILL_MITER_LIMIT)
                return Point{.x = point.miter_x, .y = point.miter_y};
            // a needle corner is pulled back to the limit, which the fringe covers at the size it reaches
            const float scale = FILL_MITER_LIMIT / magnitude;
            return Point{.x = point.miter_x * scale, .y = point.miter_y * scale};
        }

        // the side of its boundary a contour fills on, which the rings and the fill rule decide (§6.2)
        float ring_side(std::span<const FillRing> rings, const Contour& contour, FillRule rule) {
            // the normal of the first point places the two probes, a half fringe apart
            const Point miter = clamped_miter(contour.points.front());
            const Point& position = contour.points.front().position;
            // the probe on the side the normalized winding calls inside, and the one across the boundary
            const Point inner{.x = position.x + miter.x * FRINGE * 0.5f, .y = position.y + miter.y * FRINGE * 0.5f};
            const Point outer{.x = position.x - miter.x * FRINGE * 0.5f, .y = position.y - miter.y * FRINGE * 0.5f};
            // a boundary whose fill lies outside it is a hole, which the ring and the fringe mirror onto that side
            const bool outer_filled = inside_at(rings, outer, rule);
            const bool inner_filled = inside_at(rings, inner, rule);
            return outer_filled && !inner_filled ? -1.0f : 1.0f;
        }
    } // namespace

    std::vector<FillTrapezoid> sweep_fill(std::span<const FillRing> rings, FillRule rule) {
        // the trapezoids the sweep collects, in row order
        std::vector<FillTrapezoid> trapezoids;
        // every distinct row bounds one slab, which the sweep walks top down
        const std::vector<float> rows = sweep_rows(rings);
        for (std::size_t row = 0; row + 1 < rows.size(); row++)
            sweep_slab(rings, rows[row], rows[row + 1], rule, 0, trapezoids);
        return trapezoids;
    }

    EmittedRange expand_fill_group(FrameBatch& sink, std::vector<Contour>& contours, FillRule rule) {
        // a non zero group of one contour fills through the single contour expansion, which fans a convex ring exactly
        if (rule == FillRule::NonZero && contours.size() <= 1)
            return contours.empty() ? EmittedRange{} : expand_fill(sink, contours.front());
        // the range the expansion appends starts at the current index count
        const std::uint32_t index_begin = static_cast<std::uint32_t>(sink.indices.size());
        // the ring sits half a device pixel on the fill side of the boundary, which the fringe straddles
        const float offset = FRINGE * 0.5f;
        // the plain boundary of every filled contour, which the side probe measures against
        std::vector<FillRing> polygons;
        polygons.reserve(contours.size());
        for (Contour& contour : contours) {
            // a contour without a ring spans no area and joins no sweep
            FillRing polygon;
            if (contour.points.size() >= 3) {
                // the join calculation resolves the miter normals the offset ring is placed by
                calculate_joins(contour, FRINGE, Join::Miter, FILL_MITER_LIMIT);
                // the boundary contributes the winding its normalization reversed (§6.2)
                polygon.reversed = contour.reversed;
                polygon.points.reserve(contour.points.size());
                for (const ContourPoint& point : contour.points)
                    polygon.points.push_back(point.position);
            }
            polygons.push_back(std::move(polygon));
        }
        // the side of its boundary every contour fills on, which the winding of the whole group decides
        std::vector<float> sides;
        sides.reserve(contours.size());
        for (std::size_t index = 0; index < contours.size(); index++) {
            // an empty contour has no boundary to sit on
            if (polygons[index].points.empty()) {
                sides.push_back(1.0f);
                continue;
            }
            sides.push_back(ring_side(polygons, contours[index], rule));
        }
        // the offset ring of every filled contour, which the sweep and the fringe both read
        std::vector<FillRing> rings;
        rings.reserve(contours.size());
        for (std::size_t index = 0; index < contours.size(); index++) {
            // the ring keeps the winding the boundary contributes, which the fill rule reads
            FillRing ring;
            ring.reversed = polygons[index].reversed;
            if (!polygons[index].points.empty()) {
                // the ring sits a half fringe on the fill side of the boundary
                const float side = sides[index];
                const Contour& contour = contours[index];
                ring.points.reserve(contour.points.size());
                for (const ContourPoint& point : contour.points) {
                    // the miter normal is clamped, which keeps a needle corner from spiking the ring
                    const Point miter = clamped_miter(point);
                    ring.points.push_back(Point{.x = point.position.x + miter.x * side * offset, .y = point.position.y + miter.y * side * offset});
                }
            }
            rings.push_back(std::move(ring));
        }
        // the sweep fills every ring at once, which the rule cuts the holes by (§6.2)
        for (const FillTrapezoid& trapezoid : sweep_fill(rings, rule)) {
            // the two triangles cover the trapezoid, which its four corners bound
            const std::uint32_t left_top = emit_vertex(sink, trapezoid.left_top, trapezoid.top, 1.0f);
            const std::uint32_t right_top = emit_vertex(sink, trapezoid.right_top, trapezoid.top, 1.0f);
            const std::uint32_t left_bottom = emit_vertex(sink, trapezoid.left_bottom, trapezoid.bottom, 1.0f);
            const std::uint32_t right_bottom = emit_vertex(sink, trapezoid.right_bottom, trapezoid.bottom, 1.0f);
            emit_triangle(sink, left_top, right_top, left_bottom);
            emit_triangle(sink, right_top, right_bottom, left_bottom);
        }
        // the fringe of every contour follows the fill, which keeps the boundary antialiased (§6.2)
        for (std::size_t index = 0; index < contours.size(); index++) {
            // an empty contour has no boundary to ramp across
            if (rings[index].points.empty())
                continue;
            // the ring side carries full coverage while the mirrored side carries none
            const float side = sides[index];
            const Contour& contour = contours[index];
            // the strip starts at the current vertex count, which the wrap closes back to
            const std::uint32_t strip_first = static_cast<std::uint32_t>(sink.vertices.size());
            for (const ContourPoint& point : contour.points) {
                // the miter normal is clamped, which keeps a needle corner from folding the strip
                const Point miter = clamped_miter(point);
                // the ring side is the fill side, which the mirror across the boundary leaves bare
                emit_vertex(sink, point.position.x + miter.x * side * offset, point.position.y + miter.y * side * offset, 1.0f);
                emit_vertex(sink, point.position.x - miter.x * side * offset, point.position.y - miter.y * side * offset, 0.0f);
            }
            // the wrap repeats the first pair, which closes the strip through the seam
            emit_vertex(sink, sink.vertices[strip_first].x, sink.vertices[strip_first].y, sink.vertices[strip_first].lateral);
            emit_vertex(sink, sink.vertices[strip_first + 1].x, sink.vertices[strip_first + 1].y, sink.vertices[strip_first + 1].lateral);
            // the strip runs from the first corner through the wrap
            emit_strip(sink, strip_first, sink.vertices.size() - strip_first);
        }
        return EmittedRange{.index_begin = index_begin, .index_count = static_cast<std::uint32_t>(sink.indices.size()) - index_begin};
    }
} // namespace albor::render
