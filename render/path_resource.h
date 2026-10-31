#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/geometry.h"

namespace albor
{
    // how one path command consumes points, which the verb stream pairs against the point list (§6.4, AD-06)
    enum class PathVerb
    {
        // start a new contour at the next point
        MoveTo,

        // draw a line from the current position to the next point
        LineTo,

        // draw a quadratic curve from the current position through the next two points
        QuadraticTo,

        // draw a cubic curve from the current position through the next three points
        CubicTo,

        // close the current contour back to its start, which consumes no point
        Close,
    };

    // the immutable path a domain view retains off the ui thread, which replays the contours its verbs open (§6.4, AD-06)
    class PathResource
    {
    public:
        // build a path of the given verbs and points under a factory identifier (§6.4)
        PathResource(std::uint64_t id, std::span<const PathVerb> verbs, std::span<const Point> points);

        PathResource(const PathResource&) = delete;
        PathResource& operator=(const PathResource&) = delete;
        PathResource(PathResource&&) = delete;
        PathResource& operator=(PathResource&&) = delete;
        ~PathResource() = default;

        // hand out the identifier the factory stamped the path with, which the backend keys its gpu cache on (§6.4, AD-06)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the command stream in drawing order (§6.4)
        [[nodiscard]] std::span<const PathVerb> verbs() const;

        // hand out the contour points the verbs consume in order (§6.4)
        [[nodiscard]] std::span<const Point> points() const;

        // report how many points a verb stream consumes, or the largest size_t when a verb is unknown (§6.4)
        [[nodiscard]] static std::size_t point_demand(std::span<const PathVerb> verbs);

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the command stream in drawing order (§6.4)
        std::vector<PathVerb> m_verbs;

        // the contour points the verbs consume in order (§6.4)
        std::vector<Point> m_points;
    };
} // namespace albor
