#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/geometry.h"

namespace albor
{
    // the immutable batch of line segments a domain view retains off the ui thread, which replays in constant time (§6.4, AD-06)
    class LineBatch
    {
    public:
        // build a batch of the given segments under a factory identifier, where the points pair up into segments (§6.4)
        LineBatch(std::uint64_t id, std::span<const Point> points);

        LineBatch(const LineBatch&) = delete;
        LineBatch& operator=(const LineBatch&) = delete;
        LineBatch(LineBatch&&) = delete;
        LineBatch& operator=(LineBatch&&) = delete;
        ~LineBatch() = default;

        // hand out the identifier the factory stamped the batch with, which the backend keys its gpu cache on (§6.4, AD-06)
        [[nodiscard]] std::uint64_t id() const;

        // hand out the segment endpoints, where every consecutive pair is one segment (§6.4)
        [[nodiscard]] std::span<const Point> points() const;

        // hand out how many points the batch carries (§6.4)
        [[nodiscard]] std::size_t point_count() const;

        // hand out how many complete segments the batch carries, which is half the point count (§6.4)
        [[nodiscard]] std::size_t segment_count() const;

    private:
        // the identifier the factory assigned, which never repeats (§6.4, AD-06)
        std::uint64_t m_id;

        // the segment endpoints in drawing order (§6.4)
        std::vector<Point> m_points;
    };
} // namespace albor
